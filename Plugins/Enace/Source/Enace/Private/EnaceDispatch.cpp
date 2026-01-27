// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "EnaceDispatch.h"
#include "EnaceModule.h"
#include "Items/EnaceItemDefinition.h"
#include "Containers/EnaceContainerTypes.h"
#include "Tags/EnaceTags.h"
#include "Async/Async.h"

// Barrage/Artillery
#include "BarrageDispatch.h"
#include "Systems/ArtilleryDispatch.h"
#include "Systems/BarrageEntitySpawner.h"
#include "FBarragePrimitive.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EnaceDispatch)

// ═══════════════════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════════════════

void UEnaceDispatch::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Initialize libcuckoo maps
	Items = MakeShared<ItemDataMap>();
	Health = MakeShared<HealthDataMap>();
	Damage = MakeShared<DamageDataMap>();
	Loot = MakeShared<LootDataMap>();
	Containers = MakeShared<ContainerDataMap>();

	SelfPtr = this;

	UE_LOG(LogEnace, Log, TEXT("EnaceDispatch initialized"));
}

void UEnaceDispatch::Deinitialize()
{
	if (Items) Items->clear();
	if (Health) Health->clear();
	if (Damage) Damage->clear();
	if (Loot) Loot->clear();
	if (Containers) Containers->clear();

	SelfPtr = nullptr;

	Super::Deinitialize();
}

bool UEnaceDispatch::ShouldCreateSubsystem(UObject* Outer) const
{
	UWorld* World = Cast<UWorld>(Outer);
	return World && (World->IsGameWorld() || World->IsPreviewWorld());
}

bool UEnaceDispatch::RegistrationImplementation()
{
	BarrageDispatch = GetWorld()->GetSubsystem<UBarrageDispatch>();
	ArtilleryDispatch = GetWorld()->GetSubsystem<UArtilleryDispatch>();
	return BarrageDispatch != nullptr && ArtilleryDispatch != nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// ITEMS
// ═══════════════════════════════════════════════════════════════════════════

FSkeletonKey UEnaceDispatch::SpawnWorldItem(UEnaceItemDefinition* Definition, FVector Location, int32 Count, FVector InitialVelocity)
{
	if (!Definition)
	{
		UE_LOG(LogEnace, Warning, TEXT("SpawnWorldItem: Definition is null"));
		return FSkeletonKey();
	}

	if (!Definition->WorldMesh)
	{
		UE_LOG(LogEnace, Warning, TEXT("SpawnWorldItem: Definition '%s' has no WorldMesh"), *Definition->ItemId.ToString());
		return FSkeletonKey();
	}

	// Generate item key
	FSkeletonKey Key = GenerateItemKey();
	if (!Key.IsValid())
	{
		UE_LOG(LogEnace, Error, TEXT("SpawnWorldItem: Failed to generate valid item key"));
		return FSkeletonKey();
	}

	// Spawn physics body and render instance via FBarrageSpawnUtils
	FBarrageSpawnParams Params;
	Params.Mesh = Definition->WorldMesh;
	Params.WorldTransform = FTransform(Location);
	Params.MeshScale = Definition->WorldMeshScale;
	Params.PhysicsLayer = Definition->PhysicsLayer;
	Params.bAutoCollider = Definition->bAutoCollider;
	Params.ManualColliderSize = Definition->ColliderSize;
	Params.EntityKey = Key;
	Params.bIsMovable = true;
	Params.InitialVelocity = InitialVelocity;
	Params.GravityFactor = Definition->GravityFactor;

	FBarrageSpawnResult Result = FBarrageSpawnUtils::SpawnEntity(GetWorld(), Params);

	if (!Result.bSuccess)
	{
		UE_LOG(LogEnace, Warning, TEXT("SpawnWorldItem: FBarrageSpawnUtils::SpawnEntity failed for '%s'"), *Definition->ItemId.ToString());
		return FSkeletonKey();
	}

	// Register item data
	FEnaceItemData ItemData;
	ItemData.Definition = Definition;
	ItemData.Count = Count;
	ItemData.DespawnTimer = Definition->DefaultDespawnTime;

	Items->insert_or_assign(Key, ItemData);

	// Add gameplay tag for item identification
	if (ArtilleryDispatch)
	{
		ArtilleryDispatch->AddTagToEntity(Key, TAG_Enace_Item);
	}

	// If this item is a container (chest, bag, etc.), create container data for it
	if (Definition->bIsContainer && Definition->ContainerCapacity > 0)
	{
		FEnaceContainerData ContainerData;
		ContainerData.MaxSlots = Definition->ContainerCapacity;
		ContainerData.OwnerKey = FSkeletonKey();  // World container, no owner
		ContainerData.ContainerDefinition = Definition;
		ContainerData.bAllowNesting = Definition->bAllowNestedContainers;
		ContainerData.Slots.SetNum(Definition->ContainerCapacity);

		// Apply default slot filters
		if (Definition->DefaultSlotFilters.Num() > 0)
		{
			FGameplayTag DefaultFilter = Definition->DefaultSlotFilters.First();
			for (FEnaceContainerSlot& Slot : ContainerData.Slots)
			{
				Slot.SlotTypeFilter = DefaultFilter;
			}
		}

		Containers->insert_or_assign(Key, ContainerData);

		if (ArtilleryDispatch)
		{
			ArtilleryDispatch->AddTagToEntity(Key, TAG_Enace_Container);
		}

		UE_LOG(LogEnace, Log, TEXT("SpawnWorldItem: '%s' is a container with %d slots (Key: %llu)"),
			*Definition->ItemId.ToString(), Definition->ContainerCapacity, (uint64)Key);
	}

	UE_LOG(LogEnace, Verbose, TEXT("SpawnWorldItem: Spawned '%s' x%d at %s (Key: %llu)"),
		*Definition->ItemId.ToString(), Count, *Location.ToString(), Key.IsValid() ? (uint64)Key : 0);

	return Key;
}

bool UEnaceDispatch::IsItem(FSkeletonKey Key) const
{
	if (!ArtilleryDispatch)
	{
		return false;
	}
	return ArtilleryDispatch->DoesEntityHaveTag(Key, TAG_Enace_Item);
}

bool UEnaceDispatch::TryGetItemData(FSkeletonKey Key, FEnaceItemData& OutData) const
{
	if (!Items)
	{
		return false;
	}
	return Items->find(Key, OutData);
}

void UEnaceDispatch::SetItemCount(FSkeletonKey Key, int32 NewCount)
{
	FEnaceItemData Data;
	if (Items && Items->find(Key, Data))
	{
		Data.Count = NewCount;
		if (Data.Count <= 0)
		{
			DestroyItem(Key);
		}
		else
		{
			Items->insert_or_assign(Key, Data);
		}
	}
}

void UEnaceDispatch::DestroyItem(FSkeletonKey Key)
{
	// Remove physics body via tombstoning
	if (BarrageDispatch)
	{
		if (FBLet Body = BarrageDispatch->GetShapeRef(Key))
		{
			BarrageDispatch->SuggestTombstone(Body);
		}
	}

	// Remove render instance
	if (UBarrageRenderManager* RenderManager = UBarrageRenderManager::Get(GetWorld()))
	{
		RenderManager->RemoveInstance(Key);
	}

	// Unregister all data
	UnregisterAll(Key);

	UE_LOG(LogEnace, Verbose, TEXT("DestroyItem: Destroyed item (Key: %llu)"), (uint64)Key);
}

// ═══════════════════════════════════════════════════════════════════════════
// HEALTH
// ═══════════════════════════════════════════════════════════════════════════

void UEnaceDispatch::RegisterHealth(FSkeletonKey Key, float MaxHP, float CurrentHP)
{
	FEnaceHealthData Data;
	Data.MaxHP = MaxHP;
	Data.CurrentHP = (CurrentHP < 0.f) ? MaxHP : CurrentHP;

	if (Health)
	{
		Health->insert_or_assign(Key, Data);
	}

	if (ArtilleryDispatch)
	{
		ArtilleryDispatch->AddTagToEntity(Key, TAG_Enace_HasHealth);
	}
}

bool UEnaceDispatch::TryGetHealthData(FSkeletonKey Key, FEnaceHealthData& OutData) const
{
	if (!Health)
	{
		return false;
	}
	return Health->find(Key, OutData);
}

bool UEnaceDispatch::ApplyDamage(FSkeletonKey Key, float DamageAmount, FSkeletonKey Instigator)
{
	FEnaceHealthData Data;
	if (!Health || !Health->find(Key, Data))
	{
		return false;
	}

	// Apply armor reduction
	float ActualDamage = FMath::Max(0.f, DamageAmount - Data.Armor);
	Data.CurrentHP = FMath::Max(0.f, Data.CurrentHP - ActualDamage);

	Health->insert_or_assign(Key, Data);

	UE_LOG(LogEnace, Verbose, TEXT("ApplyDamage: Key %llu took %.1f damage (%.1f after armor), HP: %.1f/%.1f"),
		(uint64)Key, DamageAmount, ActualDamage, Data.CurrentHP, Data.MaxHP);

	return Data.CurrentHP <= 0.f;  // Returns true if killed
}

bool UEnaceDispatch::Heal(FSkeletonKey Key, float Amount)
{
	FEnaceHealthData Data;
	if (!Health || !Health->find(Key, Data))
	{
		return false;
	}

	Data.CurrentHP = FMath::Min(Data.MaxHP, Data.CurrentHP + Amount);
	Health->insert_or_assign(Key, Data);

	return true;
}

bool UEnaceDispatch::IsAlive(FSkeletonKey Key) const
{
	FEnaceHealthData Data;
	return Health && Health->find(Key, Data) && Data.IsAlive();
}

// ═══════════════════════════════════════════════════════════════════════════
// DAMAGE SOURCE
// ═══════════════════════════════════════════════════════════════════════════

void UEnaceDispatch::RegisterDamageSource(FSkeletonKey Key, const FEnaceDamageData& Data)
{
	if (Damage)
	{
		Damage->insert_or_assign(Key, Data);
	}
}

bool UEnaceDispatch::TryGetDamageData(FSkeletonKey Key, FEnaceDamageData& OutData) const
{
	if (!Damage)
	{
		return false;
	}
	return Damage->find(Key, OutData);
}

// ═══════════════════════════════════════════════════════════════════════════
// LOOT
// ═══════════════════════════════════════════════════════════════════════════

void UEnaceDispatch::RegisterLoot(FSkeletonKey Key, const FEnaceLootData& Data)
{
	if (Loot)
	{
		Loot->insert_or_assign(Key, Data);
	}

	if (ArtilleryDispatch)
	{
		ArtilleryDispatch->AddTagToEntity(Key, TAG_Enace_HasLoot);
	}
}

bool UEnaceDispatch::TryGetLootData(FSkeletonKey Key, FEnaceLootData& OutData) const
{
	if (!Loot)
	{
		return false;
	}
	return Loot->find(Key, OutData);
}

void UEnaceDispatch::SpawnLoot(FSkeletonKey Key, FVector Location)
{
	FEnaceLootData Data;
	if (!Loot || !Loot->find(Key, Data))
	{
		return;
	}

	// TODO: Implement loot table rolling and item spawning
	// For now, just log and remove the loot data
	UE_LOG(LogEnace, Verbose, TEXT("SpawnLoot: Would spawn %d-%d items at %s (loot table not implemented)"),
		Data.MinDrops, Data.MaxDrops, *Location.ToString());

	Loot->erase(Key);

	if (ArtilleryDispatch)
	{
		ArtilleryDispatch->RemoveTagFromEntity(Key, TAG_Enace_HasLoot);
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// CLEANUP
// ═══════════════════════════════════════════════════════════════════════════

void UEnaceDispatch::UnregisterAll(FSkeletonKey Key)
{
	if (Items) Items->erase(Key);
	if (Health) Health->erase(Key);
	if (Damage) Damage->erase(Key);
	if (Loot) Loot->erase(Key);
	if (Containers) Containers->erase(Key);

	if (ArtilleryDispatch)
	{
		ArtilleryDispatch->RemoveTagFromEntity(Key, TAG_Enace_Item);
		ArtilleryDispatch->RemoveTagFromEntity(Key, TAG_Enace_Container);
		ArtilleryDispatch->RemoveTagFromEntity(Key, TAG_Enace_HasHealth);
		ArtilleryDispatch->RemoveTagFromEntity(Key, TAG_Enace_HasLoot);
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// KEY GENERATION
// ═══════════════════════════════════════════════════════════════════════════

FSkeletonKey UEnaceDispatch::GenerateItemKey()
{
	uint32 Value = KeyCounter.fetch_add(1, std::memory_order_relaxed);

	// Combine with world pointer hash for uniqueness across worlds
	uint32 WorldHash = GetTypeHash(GetWorld());
	uint32 Combined = HashCombine(Value, WorldHash);

	// Create FItemKey which embeds SFIX_ITEM type nibble
	FItemKey ItemKey(Combined);

	if (!ItemKey.IsValid())
	{
		UE_LOG(LogEnace, Error, TEXT("GenerateItemKey: Failed to create valid FItemKey from hash %u"), Combined);
		return FSkeletonKey();
	}

	return ItemKey.AsSkeletonKey();
}

FSkeletonKey UEnaceDispatch::GenerateContainerKey()
{
	// Containers use SFIX_ITEM since they are special items
	return GenerateItemKey();
}

// ═══════════════════════════════════════════════════════════════════════════
// CONTAINERS
// ═══════════════════════════════════════════════════════════════════════════

FSkeletonKey UEnaceDispatch::CreateContainer(UEnaceItemDefinition* Definition, int32 SlotCount, FSkeletonKey Owner)
{
	FSkeletonKey Key = GenerateContainerKey();
	if (!Key.IsValid())
	{
		UE_LOG(LogEnace, Error, TEXT("CreateContainer: Failed to generate valid key"));
		return FSkeletonKey();
	}

	FEnaceContainerData Data;
	Data.MaxSlots = SlotCount;
	Data.OwnerKey = Owner;
	Data.ContainerDefinition = Definition;
	Data.bAllowNesting = Definition ? Definition->bAllowNestedContainers : true;

	// Pre-allocate slots
	int32 InitialSlots = (SlotCount > 0) ? SlotCount : 10;  // Default 10 for dynamic
	Data.Slots.SetNum(InitialSlots);

	// Apply default slot filters from definition
	if (Definition && Definition->DefaultSlotFilters.Num() > 0)
	{
		FGameplayTag DefaultFilter = Definition->DefaultSlotFilters.First();
		for (FEnaceContainerSlot& Slot : Data.Slots)
		{
			Slot.SlotTypeFilter = DefaultFilter;
		}
	}

	if (Containers)
	{
		Containers->insert_or_assign(Key, Data);
	}

	if (ArtilleryDispatch)
	{
		ArtilleryDispatch->AddTagToEntity(Key, TAG_Enace_Container);
		if (Definition)
		{
			ArtilleryDispatch->AddTagToEntity(Key, TAG_Enace_Item);
		}
	}

	// Broadcast event
	FEnaceContainerEvent Event;
	Event.EventType = EEnaceContainerEventType::ContainerCreated;
	Event.ContainerKey = Key;
	BroadcastContainerEvent(Event);

	UE_LOG(LogEnace, Verbose, TEXT("CreateContainer: Created container with %d slots (Key: %llu)"),
		InitialSlots, (uint64)Key);

	return Key;
}

FSkeletonKey UEnaceDispatch::CreateContainerFromDefinition(UEnaceItemDefinition* Definition, FSkeletonKey Owner)
{
	if (!Definition)
	{
		UE_LOG(LogEnace, Warning, TEXT("CreateContainerFromDefinition: Definition is null"));
		return FSkeletonKey();
	}

	if (!Definition->bIsContainer || Definition->ContainerCapacity <= 0)
	{
		UE_LOG(LogEnace, Warning, TEXT("CreateContainerFromDefinition: Definition '%s' is not a container"),
			*Definition->ItemId.ToString());
		return FSkeletonKey();
	}

	return CreateContainer(Definition, Definition->ContainerCapacity, Owner);
}

void UEnaceDispatch::DestroyContainer(FSkeletonKey ContainerKey)
{
	if (!Containers)
	{
		return;
	}

	FEnaceContainerData Data;
	if (!Containers->find(ContainerKey, Data))
	{
		return;
	}

	// Broadcast event before destruction
	FEnaceContainerEvent Event;
	Event.EventType = EEnaceContainerEventType::ContainerDestroyed;
	Event.ContainerKey = ContainerKey;
	BroadcastContainerEvent(Event);

	// Destroy nested containers recursively
	for (const FEnaceContainerSlot& Slot : Data.Slots)
	{
		if (!Slot.IsEmpty() && Slot.ItemDefinition && Slot.ItemDefinition->bIsContainer)
		{
			// Note: nested containers store their key in the slot
			// For now, we just clear the data - nested container data is stored inline
		}
	}

	Containers->erase(ContainerKey);

	if (ArtilleryDispatch)
	{
		ArtilleryDispatch->RemoveTagFromEntity(ContainerKey, TAG_Enace_Container);
		ArtilleryDispatch->RemoveTagFromEntity(ContainerKey, TAG_Enace_Item);
	}

	UE_LOG(LogEnace, Verbose, TEXT("DestroyContainer: Destroyed container (Key: %llu)"), (uint64)ContainerKey);
}

bool UEnaceDispatch::IsContainer(FSkeletonKey Key) const
{
	if (!ArtilleryDispatch)
	{
		return false;
	}
	return ArtilleryDispatch->DoesEntityHaveTag(Key, TAG_Enace_Container);
}

bool UEnaceDispatch::TryGetContainerData(FSkeletonKey Key, FEnaceContainerData& OutData) const
{
	if (!Containers)
	{
		return false;
	}
	return Containers->find(Key, OutData);
}

FSkeletonKey UEnaceDispatch::FindFirstWorldContainer() const
{
	if (!Containers)
	{
		return FSkeletonKey();
	}

	FSkeletonKey FoundKey;

	// Iterate through all containers, find first with no owner (world container)
	auto LockedTable = Containers->lock_table();
	for (const auto& Pair : LockedTable)
	{
		// World containers have no owner (OwnerKey is invalid)
		if (!Pair.second.OwnerKey.IsValid())
		{
			FoundKey = Pair.first;
			break;
		}
	}

	return FoundKey;
}

TArray<FSkeletonKey> UEnaceDispatch::GetAllWorldContainers() const
{
	TArray<FSkeletonKey> Result;

	if (!Containers)
	{
		return Result;
	}

	auto LockedTable = Containers->lock_table();
	for (const auto& Pair : LockedTable)
	{
		// World containers have no owner
		if (!Pair.second.OwnerKey.IsValid())
		{
			Result.Add(Pair.first);
		}
	}

	return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Container Slot Operations
// ─────────────────────────────────────────────────────────────────────────────

FEnaceAddItemResult UEnaceDispatch::AddItem(FSkeletonKey ContainerKey, UEnaceItemDefinition* Definition, int32 Count)
{
	FEnaceAddItemResult Result;

	if (!Definition || Count <= 0 || !Containers)
	{
		return Result;
	}

	FEnaceContainerData Data;
	if (!Containers->find(ContainerKey, Data))
	{
		return Result;
	}

	int32 RemainingCount = Count;

	// First, try to stack with existing items
	for (int32 i = 0; i < Data.Slots.Num() && RemainingCount > 0; i++)
	{
		FEnaceContainerSlot& Slot = Data.Slots[i];
		if (Slot.ItemDefinition == Definition && !Slot.IsEmpty())
		{
			int32 Space = Slot.GetRemainingSpace();
			if (Space > 0)
			{
				int32 ToAdd = FMath::Min(Space, RemainingCount);
				Slot.Count += ToAdd;
				RemainingCount -= ToAdd;
				Result.AddedCount += ToAdd;
				Result.SlotIndex = i;  // Track which slot we added to

				// Broadcast slot change
				FEnaceContainerEvent Event;
				Event.EventType = EEnaceContainerEventType::ItemAdded;
				Event.ContainerKey = ContainerKey;
				Event.SlotIndex = i;
				Event.ItemDefinition = Definition;
				Event.Count = Slot.Count;
				Event.PreviousCount = Slot.Count - ToAdd;
				BroadcastContainerEvent(Event);
			}
		}
	}

	// Then, use empty slots
	for (int32 i = 0; i < Data.Slots.Num() && RemainingCount > 0; i++)
	{
		FEnaceContainerSlot& Slot = Data.Slots[i];
		if (Slot.IsEmpty() && CanPlaceInSlot(Slot, Definition, Data))
		{
			int32 ToAdd = FMath::Min(Definition->MaxStackSize, RemainingCount);
			Slot.ItemDefinition = Definition;
			Slot.Count = ToAdd;
			RemainingCount -= ToAdd;
			Result.AddedCount += ToAdd;
			Result.SlotIndex = i;

			// Broadcast slot change
			FEnaceContainerEvent Event;
			Event.EventType = EEnaceContainerEventType::ItemAdded;
			Event.ContainerKey = ContainerKey;
			Event.SlotIndex = i;
			Event.ItemDefinition = Definition;
			Event.Count = ToAdd;
			Event.PreviousCount = 0;
			BroadcastContainerEvent(Event);
		}
	}

	// If dynamic container and still have items, add new slots
	if (Data.MaxSlots < 0 && RemainingCount > 0)
	{
		while (RemainingCount > 0)
		{
			FEnaceContainerSlot NewSlot;
			int32 ToAdd = FMath::Min(Definition->MaxStackSize, RemainingCount);
			NewSlot.ItemDefinition = Definition;
			NewSlot.Count = ToAdd;

			int32 NewIndex = Data.Slots.Add(NewSlot);
			RemainingCount -= ToAdd;
			Result.AddedCount += ToAdd;
			Result.SlotIndex = NewIndex;

			// Broadcast
			FEnaceContainerEvent Event;
			Event.EventType = EEnaceContainerEventType::ItemAdded;
			Event.ContainerKey = ContainerKey;
			Event.SlotIndex = NewIndex;
			Event.ItemDefinition = Definition;
			Event.Count = ToAdd;
			Event.PreviousCount = 0;
			BroadcastContainerEvent(Event);
		}
	}

	Result.OverflowCount = RemainingCount;
	Result.bSuccess = Result.AddedCount > 0;

	// Update container data
	if (Result.bSuccess)
	{
		Containers->insert_or_assign(ContainerKey, Data);

		UE_LOG(LogEnace, Log, TEXT("AddItem: Added '%s' x%d to container (Key: %llu), Slot: %d, Overflow: %d"),
			*Definition->ItemId.ToString(),
			Result.AddedCount,
			(uint64)ContainerKey,
			Result.SlotIndex,
			Result.OverflowCount);
	}
	else
	{
		UE_LOG(LogEnace, Warning, TEXT("AddItem: Failed to add '%s' x%d to container (Key: %llu) - container full"),
			*Definition->ItemId.ToString(),
			Count,
			(uint64)ContainerKey);
	}

	return Result;
}

FEnaceAddItemResult UEnaceDispatch::AddItemToSlot(FSkeletonKey ContainerKey, int32 SlotIndex, UEnaceItemDefinition* Definition, int32 Count)
{
	FEnaceAddItemResult Result;

	if (!Definition || Count <= 0 || !Containers)
	{
		return Result;
	}

	FEnaceContainerData Data;
	if (!Containers->find(ContainerKey, Data))
	{
		return Result;
	}

	if (!ValidateSlotIndex(Data, SlotIndex))
	{
		return Result;
	}

	FEnaceContainerSlot& Slot = Data.Slots[SlotIndex];

	if (!CanPlaceInSlot(Slot, Definition, Data))
	{
		return Result;
	}

	int32 PreviousCount = Slot.Count;

	if (Slot.IsEmpty())
	{
		// Empty slot - place item
		int32 ToAdd = FMath::Min(Definition->MaxStackSize, Count);
		Slot.ItemDefinition = Definition;
		Slot.Count = ToAdd;
		Result.AddedCount = ToAdd;
		Result.OverflowCount = Count - ToAdd;
	}
	else if (Slot.ItemDefinition == Definition)
	{
		// Same item - stack
		int32 Space = Slot.GetRemainingSpace();
		int32 ToAdd = FMath::Min(Space, Count);
		Slot.Count += ToAdd;
		Result.AddedCount = ToAdd;
		Result.OverflowCount = Count - ToAdd;
	}
	else
	{
		// Different item - cannot add
		Result.OverflowCount = Count;
		return Result;
	}

	Result.bSuccess = Result.AddedCount > 0;
	Result.SlotIndex = SlotIndex;

	if (Result.bSuccess)
	{
		Containers->insert_or_assign(ContainerKey, Data);

		// Broadcast
		FEnaceContainerEvent Event;
		Event.EventType = EEnaceContainerEventType::ItemAdded;
		Event.ContainerKey = ContainerKey;
		Event.SlotIndex = SlotIndex;
		Event.ItemDefinition = Definition;
		Event.Count = Slot.Count;
		Event.PreviousCount = PreviousCount;
		BroadcastContainerEvent(Event);

		UE_LOG(LogEnace, Log, TEXT("AddItemToSlot: Added '%s' x%d to container (Key: %llu), Slot: %d [%d -> %d]"),
			*Definition->ItemId.ToString(),
			Result.AddedCount,
			(uint64)ContainerKey,
			SlotIndex,
			PreviousCount,
			Slot.Count);
	}
	else
	{
		UE_LOG(LogEnace, Warning, TEXT("AddItemToSlot: Failed to add '%s' to container (Key: %llu), Slot: %d - slot incompatible or full"),
			*Definition->ItemId.ToString(),
			(uint64)ContainerKey,
			SlotIndex);
	}

	return Result;
}

bool UEnaceDispatch::RemoveItem(FSkeletonKey ContainerKey, int32 SlotIndex, int32 Count, UEnaceItemDefinition*& OutDefinition, int32& OutCount)
{
	OutDefinition = nullptr;
	OutCount = 0;

	if (!Containers)
	{
		return false;
	}

	FEnaceContainerData Data;
	if (!Containers->find(ContainerKey, Data))
	{
		return false;
	}

	if (!ValidateSlotIndex(Data, SlotIndex))
	{
		return false;
	}

	FEnaceContainerSlot& Slot = Data.Slots[SlotIndex];

	if (Slot.IsEmpty() || Slot.bIsLocked)
	{
		return false;
	}

	OutDefinition = Slot.ItemDefinition;
	int32 PreviousCount = Slot.Count;

	// -1 means remove all
	int32 ToRemove = (Count < 0) ? Slot.Count : FMath::Min(Count, Slot.Count);
	Slot.Count -= ToRemove;
	OutCount = ToRemove;

	if (Slot.Count <= 0)
	{
		Slot.Clear();
	}

	Containers->insert_or_assign(ContainerKey, Data);

	// Broadcast
	FEnaceContainerEvent Event;
	Event.EventType = EEnaceContainerEventType::ItemRemoved;
	Event.ContainerKey = ContainerKey;
	Event.SlotIndex = SlotIndex;
	Event.ItemDefinition = OutDefinition;
	Event.Count = Slot.Count;
	Event.PreviousCount = PreviousCount;
	BroadcastContainerEvent(Event);

	UE_LOG(LogEnace, Log, TEXT("RemoveItem: Removed '%s' x%d from container (Key: %llu), Slot: %d [%d -> %d]"),
		*OutDefinition->ItemId.ToString(),
		OutCount,
		(uint64)ContainerKey,
		SlotIndex,
		PreviousCount,
		Slot.Count);

	return true;
}

FEnaceMoveItemResult UEnaceDispatch::MoveItem(FSkeletonKey FromContainer, int32 FromSlot, FSkeletonKey ToContainer, int32 ToSlot, int32 Count)
{
	FEnaceMoveItemResult Result;

	if (!Containers)
	{
		return Result;
	}

	// Get source data
	FEnaceContainerData FromData;
	if (!Containers->find(FromContainer, FromData))
	{
		return Result;
	}

	if (!ValidateSlotIndex(FromData, FromSlot))
	{
		return Result;
	}

	FEnaceContainerSlot& SourceSlot = FromData.Slots[FromSlot];
	if (SourceSlot.IsEmpty() || SourceSlot.bIsLocked)
	{
		return Result;
	}

	// Get destination data
	FEnaceContainerData ToData;
	bool bSameContainer = (FromContainer == ToContainer);

	if (bSameContainer)
	{
		ToData = FromData;
	}
	else if (!Containers->find(ToContainer, ToData))
	{
		return Result;
	}

	if (!ValidateSlotIndex(ToData, ToSlot))
	{
		return Result;
	}

	FEnaceContainerSlot& DestSlot = bSameContainer ? FromData.Slots[ToSlot] : ToData.Slots[ToSlot];

	if (DestSlot.bIsLocked)
	{
		return Result;
	}

	if (!CanPlaceInSlot(DestSlot, SourceSlot.ItemDefinition, ToData))
	{
		return Result;
	}

	int32 ToMove = (Count < 0) ? SourceSlot.Count : FMath::Min(Count, SourceSlot.Count);

	if (DestSlot.IsEmpty())
	{
		// Move to empty slot
		DestSlot.ItemDefinition = SourceSlot.ItemDefinition;
		DestSlot.Count = ToMove;
		SourceSlot.Count -= ToMove;
		if (SourceSlot.Count <= 0)
		{
			SourceSlot.Clear();
		}
		Result.MovedCount = ToMove;
		Result.bSuccess = true;
	}
	else if (DestSlot.ItemDefinition == SourceSlot.ItemDefinition)
	{
		// Stack with same item
		int32 Space = DestSlot.GetRemainingSpace();
		int32 ActualMove = FMath::Min(ToMove, Space);
		DestSlot.Count += ActualMove;
		SourceSlot.Count -= ActualMove;
		if (SourceSlot.Count <= 0)
		{
			SourceSlot.Clear();
		}
		Result.MovedCount = ActualMove;
		Result.bSuccess = ActualMove > 0;
	}

	if (Result.bSuccess)
	{
		Containers->insert_or_assign(FromContainer, FromData);
		if (!bSameContainer)
		{
			Containers->insert_or_assign(ToContainer, ToData);
		}

		// Broadcast events
		FEnaceContainerEvent FromEvent;
		FromEvent.EventType = EEnaceContainerEventType::ItemMoved;
		FromEvent.ContainerKey = FromContainer;
		FromEvent.SlotIndex = FromSlot;
		BroadcastContainerEvent(FromEvent);

		FEnaceContainerEvent ToEvent;
		ToEvent.EventType = EEnaceContainerEventType::ItemMoved;
		ToEvent.ContainerKey = ToContainer;
		ToEvent.SlotIndex = ToSlot;
		BroadcastContainerEvent(ToEvent);

		UE_LOG(LogEnace, Log, TEXT("MoveItem: Moved x%d from container %llu[%d] to container %llu[%d]"),
			Result.MovedCount,
			(uint64)FromContainer,
			FromSlot,
			(uint64)ToContainer,
			ToSlot);
	}

	return Result;
}

bool UEnaceDispatch::SwapSlots(FSkeletonKey ContainerA, int32 SlotA, FSkeletonKey ContainerB, int32 SlotB)
{
	if (!Containers)
	{
		return false;
	}

	FEnaceContainerData DataA;
	if (!Containers->find(ContainerA, DataA))
	{
		return false;
	}

	if (!ValidateSlotIndex(DataA, SlotA))
	{
		return false;
	}

	bool bSameContainer = (ContainerA == ContainerB);
	FEnaceContainerData DataB = bSameContainer ? DataA : FEnaceContainerData();

	if (!bSameContainer && !Containers->find(ContainerB, DataB))
	{
		return false;
	}

	if (!ValidateSlotIndex(DataB, SlotB))
	{
		return false;
	}

	FEnaceContainerSlot& SlotDataA = DataA.Slots[SlotA];
	FEnaceContainerSlot& SlotDataB = bSameContainer ? DataA.Slots[SlotB] : DataB.Slots[SlotB];

	if (SlotDataA.bIsLocked || SlotDataB.bIsLocked)
	{
		return false;
	}

	// Check type filters both ways
	if (!SlotDataA.IsEmpty() && SlotDataB.SlotTypeFilter.IsValid())
	{
		if (!SlotDataA.ItemDefinition->ItemTags.HasTag(SlotDataB.SlotTypeFilter))
		{
			return false;
		}
	}
	if (!SlotDataB.IsEmpty() && SlotDataA.SlotTypeFilter.IsValid())
	{
		if (!SlotDataB.ItemDefinition->ItemTags.HasTag(SlotDataA.SlotTypeFilter))
		{
			return false;
		}
	}

	// Swap (preserve filters and lock state)
	FGameplayTag FilterA = SlotDataA.SlotTypeFilter;
	bool bLockedA = SlotDataA.bIsLocked;
	FGameplayTag FilterB = SlotDataB.SlotTypeFilter;
	bool bLockedB = SlotDataB.bIsLocked;

	Swap(SlotDataA.ItemDefinition, SlotDataB.ItemDefinition);
	Swap(SlotDataA.Count, SlotDataB.Count);

	SlotDataA.SlotTypeFilter = FilterA;
	SlotDataA.bIsLocked = bLockedA;
	SlotDataB.SlotTypeFilter = FilterB;
	SlotDataB.bIsLocked = bLockedB;

	Containers->insert_or_assign(ContainerA, DataA);
	if (!bSameContainer)
	{
		Containers->insert_or_assign(ContainerB, DataB);
	}

	// Broadcast
	FEnaceContainerEvent EventA;
	EventA.EventType = EEnaceContainerEventType::SlotChanged;
	EventA.ContainerKey = ContainerA;
	EventA.SlotIndex = SlotA;
	BroadcastContainerEvent(EventA);

	FEnaceContainerEvent EventB;
	EventB.EventType = EEnaceContainerEventType::SlotChanged;
	EventB.ContainerKey = ContainerB;
	EventB.SlotIndex = SlotB;
	BroadcastContainerEvent(EventB);

	UE_LOG(LogEnace, Log, TEXT("SwapSlots: Swapped container %llu[%d] <-> container %llu[%d]"),
		(uint64)ContainerA,
		SlotA,
		(uint64)ContainerB,
		SlotB);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Container Queries
// ─────────────────────────────────────────────────────────────────────────────

bool UEnaceDispatch::TryGetSlot(FSkeletonKey ContainerKey, int32 SlotIndex, FEnaceContainerSlot& OutSlot) const
{
	if (!Containers)
	{
		return false;
	}

	FEnaceContainerData Data;
	if (!Containers->find(ContainerKey, Data))
	{
		return false;
	}

	if (!ValidateSlotIndex(Data, SlotIndex))
	{
		return false;
	}

	OutSlot = Data.Slots[SlotIndex];
	return true;
}

int32 UEnaceDispatch::GetSlotCount(FSkeletonKey ContainerKey) const
{
	if (!Containers)
	{
		return 0;
	}

	FEnaceContainerData Data;
	if (!Containers->find(ContainerKey, Data))
	{
		return 0;
	}

	return Data.Slots.Num();
}

int32 UEnaceDispatch::GetUsedSlotCount(FSkeletonKey ContainerKey) const
{
	if (!Containers)
	{
		return 0;
	}

	FEnaceContainerData Data;
	if (!Containers->find(ContainerKey, Data))
	{
		return 0;
	}

	return Data.GetUsedSlotCount();
}

int32 UEnaceDispatch::FindFirstEmptySlot(FSkeletonKey ContainerKey) const
{
	if (!Containers)
	{
		return -1;
	}

	FEnaceContainerData Data;
	if (!Containers->find(ContainerKey, Data))
	{
		return -1;
	}

	return Data.FindFirstEmptySlot();
}

int32 UEnaceDispatch::FindItemSlot(FSkeletonKey ContainerKey, UEnaceItemDefinition* Definition) const
{
	if (!Containers || !Definition)
	{
		return -1;
	}

	FEnaceContainerData Data;
	if (!Containers->find(ContainerKey, Data))
	{
		return -1;
	}

	return Data.FindItemSlot(Definition);
}

int32 UEnaceDispatch::GetItemCount(FSkeletonKey ContainerKey, UEnaceItemDefinition* Definition) const
{
	if (!Containers || !Definition)
	{
		return 0;
	}

	FEnaceContainerData Data;
	if (!Containers->find(ContainerKey, Data))
	{
		return 0;
	}

	int32 Total = 0;
	for (const FEnaceContainerSlot& Slot : Data.Slots)
	{
		if (Slot.ItemDefinition == Definition)
		{
			Total += Slot.Count;
		}
	}

	return Total;
}

bool UEnaceDispatch::HasItem(FSkeletonKey ContainerKey, UEnaceItemDefinition* Definition, int32 MinCount) const
{
	return GetItemCount(ContainerKey, Definition) >= MinCount;
}

// ─────────────────────────────────────────────────────────────────────────────
// Container Slot Configuration
// ─────────────────────────────────────────────────────────────────────────────

bool UEnaceDispatch::SetSlotTypeFilter(FSkeletonKey ContainerKey, int32 SlotIndex, FGameplayTag TypeFilter)
{
	if (!Containers)
	{
		return false;
	}

	FEnaceContainerData Data;
	if (!Containers->find(ContainerKey, Data))
	{
		return false;
	}

	if (!ValidateSlotIndex(Data, SlotIndex))
	{
		return false;
	}

	Data.Slots[SlotIndex].SlotTypeFilter = TypeFilter;
	Containers->insert_or_assign(ContainerKey, Data);

	return true;
}

bool UEnaceDispatch::SetSlotLocked(FSkeletonKey ContainerKey, int32 SlotIndex, bool bLocked)
{
	if (!Containers)
	{
		return false;
	}

	FEnaceContainerData Data;
	if (!Containers->find(ContainerKey, Data))
	{
		return false;
	}

	if (!ValidateSlotIndex(Data, SlotIndex))
	{
		return false;
	}

	Data.Slots[SlotIndex].bIsLocked = bLocked;
	Containers->insert_or_assign(ContainerKey, Data);

	return true;
}

bool UEnaceDispatch::AddSlots(FSkeletonKey ContainerKey, int32 Count)
{
	if (!Containers || Count <= 0)
	{
		return false;
	}

	FEnaceContainerData Data;
	if (!Containers->find(ContainerKey, Data))
	{
		return false;
	}

	// Check if fixed-size container
	if (Data.MaxSlots > 0 && Data.Slots.Num() + Count > Data.MaxSlots)
	{
		return false;
	}

	for (int32 i = 0; i < Count; i++)
	{
		Data.Slots.Add(FEnaceContainerSlot());
	}

	Containers->insert_or_assign(ContainerKey, Data);

	return true;
}

bool UEnaceDispatch::RemoveEmptySlots(FSkeletonKey ContainerKey, int32 Count)
{
	if (!Containers || Count <= 0)
	{
		return false;
	}

	FEnaceContainerData Data;
	if (!Containers->find(ContainerKey, Data))
	{
		return false;
	}

	// Fixed-size containers cannot remove slots
	if (Data.MaxSlots > 0)
	{
		return false;
	}

	int32 Removed = 0;
	while (Removed < Count && Data.Slots.Num() > 0)
	{
		if (Data.Slots.Last().IsEmpty())
		{
			Data.Slots.RemoveAt(Data.Slots.Num() - 1);
			Removed++;
		}
		else
		{
			break;
		}
	}

	if (Removed > 0)
	{
		Containers->insert_or_assign(ContainerKey, Data);
	}

	return Removed > 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal Helpers
// ─────────────────────────────────────────────────────────────────────────────

void UEnaceDispatch::BroadcastContainerEvent(const FEnaceContainerEvent& Event)
{
	// Broadcast on game thread for UI safety
	if (IsInGameThread())
	{
		OnContainerChanged.Broadcast(Event);
		OnContainerChangedNative.Broadcast(Event);
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, [this, Event]()
		{
			if (this && IsValid(this))
			{
				OnContainerChanged.Broadcast(Event);
				OnContainerChangedNative.Broadcast(Event);
			}
		});
	}
}

bool UEnaceDispatch::ValidateSlotIndex(const FEnaceContainerData& Data, int32 SlotIndex) const
{
	return SlotIndex >= 0 && SlotIndex < Data.Slots.Num();
}

bool UEnaceDispatch::CanPlaceInSlot(const FEnaceContainerSlot& Slot, const UEnaceItemDefinition* Definition, const FEnaceContainerData& ContainerData) const
{
	if (!Definition)
	{
		return false;
	}

	if (Slot.bIsLocked)
	{
		return false;
	}

	// Check type filter
	if (Slot.SlotTypeFilter.IsValid())
	{
		if (!Definition->ItemTags.HasTag(Slot.SlotTypeFilter))
		{
			return false;
		}
	}

	// Check nesting
	if (Definition->bIsContainer && !ContainerData.bAllowNesting)
	{
		return false;
	}

	return true;
}
