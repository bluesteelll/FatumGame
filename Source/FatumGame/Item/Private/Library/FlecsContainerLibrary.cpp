
#include "FlecsContainerLibrary.h"
#include "FlecsLibraryHelpers.h"
#include "FlecsItemComponents.h"
#include "FlecsGameTags.h"
#include "FlecsEntityDefinition.h"
#include "FlecsItemDefinition.h"
#include "FlecsMagazineProfile.h"
#include "FlecsAmmoTypeDefinition.h"
#include "FlecsUISubsystem.h"
#include "FlecsVitalsComponents.h"
#include "FlecsWeaponComponents.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlecsContainer, Log, All);

/** Mark the container owner's vitals equipment cache as dirty (sim thread). */
static void MarkOwnerEquipmentDirty(int64 ContainerEntityId, flecs::world* FlecsWorld)
{
	if (!FlecsWorld || ContainerEntityId == 0) return;
	flecs::entity ContainerEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(ContainerEntityId));
	if (!ContainerEntity.is_valid()) return;
	const FContainerInstance* CI = ContainerEntity.try_get<FContainerInstance>();
	if (!CI || CI->OwnerEntityId == 0) return;
	flecs::entity OwnerEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(CI->OwnerEntityId));
	if (!OwnerEntity.is_valid()) return;
	FVitalsInstance* VI = OwnerEntity.try_get_mut<FVitalsInstance>();
	if (VI) VI->bEquipmentDirty = true;
}

/** Push fresh snapshot to triple buffer if a UI model is tracking this container. Called on sim thread. */
static void NotifyContainerUI(int64 ContainerEntityId)
{
	UFlecsUISubsystem* UISub = UFlecsUISubsystem::SelfPtr;
	if (!UISub) return;

	FContainerSharedState* Shared = UISub->FindContainerSharedState(ContainerEntityId);
	if (!Shared) return;

	FContainerSnapshot Snap = UISub->BuildContainerSnapshot(ContainerEntityId);
	Shared->SnapshotBuffer.WriteAndSwap(MoveTemp(Snap));
	Shared->SimVersion.fetch_add(1, std::memory_order_release);
}

static flecs::entity ResolveContainerEntity(UFlecsArtillerySubsystem* Sub, int64 EntityId)
{
	if (EntityId == 0) return flecs::entity();
	flecs::world* World = Sub->GetFlecsWorld();
	if (!World) return flecs::entity();
	flecs::entity E = World->entity(static_cast<flecs::entity_t>(EntityId));
	if (!E.is_alive() || !E.has<FTagContainer>()) return flecs::entity();
	return E;
}

// ═══════════════════════════════════════════════════════════════
// ITEM OPERATIONS
// ═══════════════════════════════════════════════════════════════

void UFlecsContainerLibrary::SetItemDespawnTimer(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Timer)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !BarrageKey.IsValid()) return;

	FSkeletonKey CapturedKey = BarrageKey;
	Subsystem->EnqueueCommand([Subsystem, CapturedKey, Timer]()
	{
		flecs::entity Entity = FlecsLibrary::GetEntityForKey(Subsystem, CapturedKey);
		if (Entity.is_valid() && Entity.is_alive())
		{
			FWorldItemInstance* WorldItem = Entity.try_get_mut<FWorldItemInstance>();
			if (WorldItem)
			{
				WorldItem->DespawnTimer = Timer;
			}
		}
	});
}

// ═══════════════════════════════════════════════════════════════
// CONTAINER OPERATIONS
// ═══════════════════════════════════════════════════════════════

/** Core container-add logic. MUST be called on simulation thread only.
 *  Returns number of items actually added (0 = container full). */
static int32 AddItemToContainerDirect(
	UFlecsArtillerySubsystem* Subsystem,
	int64 ContainerEntityId,
	UFlecsEntityDefinition* EntityDefinition,
	int32 Count,
	bool bAutoStack)
{
	flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
	if (!FlecsWorld) return 0;

	flecs::entity ContainerEntity = ResolveContainerEntity(Subsystem, ContainerEntityId);
	if (!ContainerEntity.is_valid())
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainerDirect: Container %lld not found or not a container"), ContainerEntityId);
		return 0;
	}

	flecs::entity ItemPrefab = Subsystem->GetOrCreateItemPrefab(EntityDefinition);
	if (!ItemPrefab.is_valid())
	{
		const FName ItemName = EntityDefinition->ItemDefinition ? EntityDefinition->ItemDefinition->ItemName : NAME_None;
		UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainerDirect: Failed to create prefab for '%s'"),
			*ItemName.ToString());
		return 0;
	}

	const FContainerStatic* ContainerStatic = ContainerEntity.try_get<FContainerStatic>();
	if (!ContainerStatic)
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainerDirect: Container %lld has no FContainerStatic"), ContainerEntityId);
		return 0;
	}

	FContainerInstance* ContainerInstance = ContainerEntity.try_get_mut<FContainerInstance>();
	if (!ContainerInstance)
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainerDirect: Container %lld has no FContainerInstance"), ContainerEntityId);
		return 0;
	}

	const FItemStaticData* StaticData = ItemPrefab.try_get<FItemStaticData>();
	const FName ItemName = EntityDefinition->ItemDefinition ? EntityDefinition->ItemDefinition->ItemName : NAME_None;
	int32 Remaining = Count;

	// Auto-stack into existing items of the same type (controlled by bAutoStack parameter)
	if (bAutoStack && StaticData && StaticData->IsStackable())
	{
		FlecsWorld->each([ContainerEntityId, &ItemName, &Remaining, StaticData](
			flecs::entity E, const FContainedIn& Contained, FItemInstance& Inst)
		{
			if (Remaining <= 0) return;
			if (Contained.ContainerEntityId != ContainerEntityId) return;

			const FItemStaticData* ExistingStatic = E.try_get<FItemStaticData>();
			if (!ExistingStatic || ExistingStatic->ItemName != ItemName) return;
			if (Inst.Count >= ExistingStatic->MaxStack) return;

			const int32 Space = ExistingStatic->MaxStack - Inst.Count;
			const int32 ToTransfer = FMath::Min(Remaining, Space);
			Inst.Count += ToTransfer;
			Remaining -= ToTransfer;
		});

		if (Remaining <= 0)
		{
			UE_LOG(LogFlecsContainer, Log, TEXT("AddItemToContainerDirect: Stacked all %d '%s' into existing items in container %lld"),
				Count, *ItemName.ToString(), ContainerEntityId);
			NotifyContainerUI(ContainerEntityId);
			return Count;
		}
	}

	auto CreateItemEntity = [&](FIntPoint GridPos, int32 SlotIdx, int32 ItemCount) -> flecs::entity
	{
		FItemInstance Instance;
		Instance.Count = ItemCount;

		FContainedIn Contained;
		Contained.ContainerEntityId = ContainerEntityId;
		Contained.GridPosition = GridPos;
		Contained.SlotIndex = SlotIdx;

		flecs::entity NewEntity = FlecsWorld->entity()
			.is_a(ItemPrefab)
			.set<FItemInstance>(Instance)
			.set<FContainedIn>(Contained)
			.add<FTagItem>();

		// Initialize magazine ammo stack if this is a magazine entity
		const FMagazineStatic* MagStatic = NewEntity.try_get<FMagazineStatic>();
		if (MagStatic)
		{
			FMagazineInstance MagInst;
			// Fill with default ammo type if the definition has one
			if (EntityDefinition && EntityDefinition->MagazineProfile && EntityDefinition->MagazineProfile->DefaultAmmoType)
			{
				int32 AmmoIdx = MagStatic->FindAmmoTypeIndex(EntityDefinition->MagazineProfile->DefaultAmmoType);
				if (AmmoIdx >= 0)
				{
					for (int32 r = 0; r < MagStatic->Capacity; ++r)
						MagInst.Push(static_cast<uint8>(AmmoIdx));
				}
			}
			NewEntity.set<FMagazineInstance>(MagInst);
			NewEntity.add<FTagMagazine>();
		}

		// Initialize weapon instance if this is a weapon entity
		if (NewEntity.has<FTagWeapon>())
		{
			FWeaponInstance WepInst;
			NewEntity.set<FWeaponInstance>(WepInst);
		}

		return NewEntity;
	};

	if (ContainerStatic->Type == EContainerType::List)
	{
		if (ContainerStatic->MaxItems > 0 && ContainerInstance->CurrentCount >= ContainerStatic->MaxItems)
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainerDirect: List container %lld is full"), ContainerEntityId);
			const int32 Added = Count - Remaining;
			if (Added > 0) NotifyContainerUI(ContainerEntityId);
			return Added;
		}

		CreateItemEntity(FIntPoint(-1, -1), ContainerInstance->CurrentCount, Remaining);
		ContainerInstance->CurrentCount++;

		UE_LOG(LogFlecsContainer, Log, TEXT("AddItemToContainerDirect: Added '%s' (Count=%d) to container %lld"),
			*ItemName.ToString(), Remaining, ContainerEntityId);
	}
	else if (ContainerStatic->Type == EContainerType::Grid)
	{
		FContainerGridInstance* GridInstance = ContainerEntity.try_get_mut<FContainerGridInstance>();
		if (!GridInstance)
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainerDirect: Grid container %lld has no FContainerGridInstance"), ContainerEntityId);
			const int32 Added = Count - Remaining;
			if (Added > 0) NotifyContainerUI(ContainerEntityId);
			return Added;
		}

		FIntPoint ItemSize = StaticData ? StaticData->GridSize : FIntPoint(1, 1);

		FIntPoint FreePos = GridInstance->FindFreeSpace(ItemSize, ContainerStatic->GridWidth, ContainerStatic->GridHeight);
		if (FreePos.X < 0)
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainerDirect: Grid container %lld is full"), ContainerEntityId);
			const int32 Added = Count - Remaining;
			if (Added > 0) NotifyContainerUI(ContainerEntityId);
			return Added;
		}

		GridInstance->Occupy(FreePos, ItemSize, ContainerStatic->GridWidth);
		CreateItemEntity(FreePos, -1, Remaining);
		ContainerInstance->CurrentCount++;

		UE_LOG(LogFlecsContainer, Log, TEXT("AddItemToContainerDirect: Added '%s' (Count=%d) to grid container %lld at (%d,%d)"),
			*ItemName.ToString(), Remaining, ContainerEntityId, FreePos.X, FreePos.Y);
	}
	else
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainerDirect: Unsupported container type %d"),
			static_cast<int32>(ContainerStatic->Type));
		const int32 Added = Count - Remaining;
		if (Added > 0) NotifyContainerUI(ContainerEntityId);
		return Added;
	}

	NotifyContainerUI(ContainerEntityId);
	MarkOwnerEquipmentDirty(ContainerEntityId, FlecsWorld);
	return Count;  // All items added: stacked portion + new entity with Remaining
}

// ═══════════════════════════════════════════════════════════════
// PlaceExistingEntityInContainer — SIM THREAD ONLY
// Places an already-existing entity (e.g. empty magazine returned from weapon)
// back into a container with proper grid placement and counter updates.
// ═══════════════════════════════════════════════════════════════
bool UFlecsContainerLibrary::PlaceExistingEntityInContainer(
	UFlecsArtillerySubsystem* Subsystem,
	int64 EntityId,
	int64 ContainerEntityId)
{
	if (!Subsystem || EntityId == 0 || ContainerEntityId == 0) return false;

	flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
	if (!FlecsWorld) return false;

	flecs::entity Entity = FlecsWorld->entity(static_cast<flecs::entity_t>(EntityId));
	if (!Entity.is_valid() || !Entity.is_alive()) return false;

	flecs::entity ContainerEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(ContainerEntityId));
	if (!ContainerEntity.is_valid() || !ContainerEntity.is_alive()) return false;

	const FContainerStatic* ContainerStatic = ContainerEntity.try_get<FContainerStatic>();
	FContainerInstance* ContainerInstance = ContainerEntity.try_get_mut<FContainerInstance>();
	if (!ContainerStatic || !ContainerInstance) return false;

	const FItemStaticData* ItemStatic = Entity.try_get<FItemStaticData>();
	FIntPoint ItemSize = ItemStatic ? ItemStatic->GridSize : FIntPoint(1, 1);

	FContainedIn Contained;
	Contained.ContainerEntityId = ContainerEntityId;

	if (ContainerStatic->Type == EContainerType::Grid)
	{
		FContainerGridInstance* Grid = ContainerEntity.try_get_mut<FContainerGridInstance>();
		if (!Grid) return false;

		FIntPoint FreePos = Grid->FindFreeSpace(ItemSize, ContainerStatic->GridWidth, ContainerStatic->GridHeight);
		if (FreePos.X < 0)
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("PlaceExistingEntityInContainer: No free space in grid container %lld"), ContainerEntityId);
			return false;
		}

		Grid->Occupy(FreePos, ItemSize, ContainerStatic->GridWidth);
		Contained.GridPosition = FreePos;
		Contained.SlotIndex = -1;
	}
	else // List
	{
		if (ContainerStatic->MaxItems > 0 && ContainerInstance->CurrentCount >= ContainerStatic->MaxItems)
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("PlaceExistingEntityInContainer: List container %lld is full"), ContainerEntityId);
			return false;
		}
		Contained.GridPosition = FIntPoint(-1, -1);
		Contained.SlotIndex = ContainerInstance->CurrentCount;
	}

	Entity.set<FContainedIn>(Contained);
	ContainerInstance->CurrentCount++;

	// Update weight
	if (ItemStatic)
	{
		const FItemInstance* ItemInst = Entity.try_get<FItemInstance>();
		int32 Count = ItemInst ? ItemInst->Count : 1;
		ContainerInstance->CurrentWeight += ItemStatic->Weight * Count;
	}

	NotifyContainerUI(ContainerEntityId);
	MarkOwnerEquipmentDirty(ContainerEntityId, FlecsWorld);

	UE_LOG(LogTemp, Log, TEXT("PlaceExistingEntityInContainer: Entity %lld placed in container %lld at (%d,%d)"),
		EntityId, ContainerEntityId, Contained.GridPosition.X, Contained.GridPosition.Y);
	return true;
}

bool UFlecsContainerLibrary::AddItemToContainer(
	UObject* WorldContextObject,
	int64 ContainerEntityId,
	UFlecsEntityDefinition* EntityDefinition,
	int32 Count,
	int32& OutActuallyAdded,
	bool bAutoStack)
{
	OutActuallyAdded = 0;

	if (ContainerEntityId == 0 || !EntityDefinition || Count <= 0)
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: Invalid parameters"));
		return false;
	}

	UFlecsItemDefinition* ItemDef = EntityDefinition->ItemDefinition;
	if (!ItemDef)
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: EntityDefinition '%s' has no ItemDefinition"),
			*EntityDefinition->GetName());
		return false;
	}

	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem)
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: No FlecsSubsystem"));
		return false;
	}

	UFlecsEntityDefinition* CapturedEntityDef = EntityDefinition;

	Subsystem->EnqueueCommand([Subsystem, ContainerEntityId, CapturedEntityDef, Count, bAutoStack]()
	{
		AddItemToContainerDirect(Subsystem, ContainerEntityId, CapturedEntityDef, Count, bAutoStack);
	});

	OutActuallyAdded = Count;
	return true;
}

bool UFlecsContainerLibrary::RemoveItemFromContainer(
	UObject* WorldContextObject,
	int64 ContainerEntityId,
	int64 ItemEntityId,
	int32 Count)
{
	if (ContainerEntityId == 0 || ItemEntityId == 0)
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("RemoveItemFromContainer: Invalid parameters"));
		return false;
	}

	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	Subsystem->EnqueueCommand([Subsystem, ContainerEntityId, ItemEntityId, Count]()
	{
		flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		flecs::entity ContainerEntity = ResolveContainerEntity(Subsystem, ContainerEntityId);
		if (!ContainerEntity.is_valid()) return;

		flecs::entity ItemEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(ItemEntityId));
		if (!ItemEntity.is_alive())
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("RemoveItemFromContainer: Item entity %lld not found"), ItemEntityId);
			return;
		}

		const FContainedIn* ContainedIn = ItemEntity.try_get<FContainedIn>();
		if (!ContainedIn || ContainedIn->ContainerEntityId != ContainerEntityId)
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("RemoveItemFromContainer: Item %lld not in container %lld"),
				ItemEntityId, ContainerEntityId);
			return;
		}

		const FContainerStatic* ContainerStatic = ContainerEntity.try_get<FContainerStatic>();
		FContainerInstance* ContainerInstance = ContainerEntity.try_get_mut<FContainerInstance>();

		if (ContainerStatic && ContainerStatic->Type == EContainerType::Grid && ContainedIn->IsInGrid())
		{
			if (FContainerGridInstance* GridInstance = ContainerEntity.try_get_mut<FContainerGridInstance>())
			{
				const FItemStaticData* ItemStatic = ItemEntity.try_get<FItemStaticData>();
				FIntPoint ItemSize = ItemStatic ? ItemStatic->GridSize : FIntPoint(1, 1);
				GridInstance->Free(ContainedIn->GridPosition, ItemSize, ContainerStatic->GridWidth);
			}
		}

		if (ContainerInstance)
		{
			ContainerInstance->CurrentCount = FMath::Max(0, ContainerInstance->CurrentCount - 1);
		}

		UE_LOG(LogFlecsContainer, Log, TEXT("RemoveItemFromContainer: Removed item %lld from container %lld"),
			ItemEntityId, ContainerEntityId);
		ItemEntity.destruct();
		NotifyContainerUI(ContainerEntityId);
		MarkOwnerEquipmentDirty(ContainerEntityId, FlecsWorld);
	});

	return true;
}

int32 UFlecsContainerLibrary::RemoveAllItemsFromContainer(
	UObject* WorldContextObject,
	int64 ContainerEntityId)
{
	if (ContainerEntityId == 0)
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("RemoveAllItemsFromContainer: Invalid ContainerEntityId"));
		return 0;
	}

	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem) return 0;

	Subsystem->EnqueueCommand([Subsystem, ContainerEntityId]()
	{
		flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		flecs::entity ContainerEntity = ResolveContainerEntity(Subsystem, ContainerEntityId);
		if (!ContainerEntity.is_valid())
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("RemoveAllItemsFromContainer: Container %lld not found"), ContainerEntityId);
			return;
		}

		TArray<flecs::entity> ItemsToRemove;
		FlecsWorld->each([ContainerEntityId, &ItemsToRemove](flecs::entity E, const FContainedIn& ContainedIn)
		{
			if (ContainedIn.ContainerEntityId == ContainerEntityId)
			{
				ItemsToRemove.Add(E);
			}
		});

		for (flecs::entity ItemEntity : ItemsToRemove)
		{
			ItemEntity.destruct();
		}

		const FContainerStatic* ContainerStatic = ContainerEntity.try_get<FContainerStatic>();
		FContainerInstance* ContainerInstance = ContainerEntity.try_get_mut<FContainerInstance>();

		if (ContainerInstance)
		{
			ContainerInstance->CurrentCount = 0;
		}

		if (ContainerStatic && ContainerStatic->Type == EContainerType::Grid)
		{
			if (FContainerGridInstance* GridInstance = ContainerEntity.try_get_mut<FContainerGridInstance>())
			{
				GridInstance->Initialize(ContainerStatic->GridWidth, ContainerStatic->GridHeight);
			}
		}

		UE_LOG(LogFlecsContainer, Log, TEXT("RemoveAllItemsFromContainer: Removed %d items from container %lld"),
			ItemsToRemove.Num(), ContainerEntityId);
		NotifyContainerUI(ContainerEntityId);
		MarkOwnerEquipmentDirty(ContainerEntityId, FlecsWorld);
	});

	return 0;
}

int32 UFlecsContainerLibrary::GetContainerItemCount(
	UObject* WorldContextObject,
	int64 ContainerEntityId)
{
	if (ContainerEntityId == 0) return -1;

	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !Subsystem->GetFlecsWorld()) return -1;

	flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
	flecs::entity ContainerEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(ContainerEntityId));
	if (!ContainerEntity.is_alive() || !ContainerEntity.has<FTagContainer>()) return -1;

	const FContainerInstance* ContainerInstance = ContainerEntity.try_get<FContainerInstance>();
	return ContainerInstance ? ContainerInstance->CurrentCount : 0;
}

bool UFlecsContainerLibrary::PickupWorldItem(
	UFlecsArtillerySubsystem* Subsystem,
	int64 WorldItemEntityId,
	int64 ContainerEntityId,
	int32& OutPickedUp)
{
	OutPickedUp = 0;

	check(Subsystem);  // SIM THREAD ONLY — Subsystem must be valid
	if (WorldItemEntityId == 0 || ContainerEntityId == 0) return false;

	flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
	if (!FlecsWorld) return false;

	flecs::entity WorldItem = FlecsWorld->entity(static_cast<flecs::entity_t>(WorldItemEntityId));
	if (!WorldItem.is_alive() || !WorldItem.has<FTagItem>())
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("PickupWorldItem: Entity %lld not found or not an item"), WorldItemEntityId);
		return false;
	}

	const FItemStaticData* ItemStatic = WorldItem.try_get<FItemStaticData>();
	if (!ItemStatic || !ItemStatic->EntityDefinition)
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("PickupWorldItem: Entity %lld has no FItemStaticData or EntityDefinition"), WorldItemEntityId);
		return false;
	}

	const FItemInstance* ItemInst = WorldItem.try_get<FItemInstance>();
	if (!ItemInst || ItemInst->Count <= 0)
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("PickupWorldItem: Entity %lld has no FItemInstance or Count <= 0"), WorldItemEntityId);
		return false;
	}

	const int32 Count = ItemInst->Count;
	UFlecsEntityDefinition* EntityDef = ItemStatic->EntityDefinition;

	const int32 Added = AddItemToContainerDirect(Subsystem, ContainerEntityId, EntityDef, Count, true);
	OutPickedUp = Added;

	if (Added <= 0) return false;

	if (Added >= Count)
	{
		WorldItem.add<FTagDead>();
	}
	else
	{
		FItemInstance* MutableInst = WorldItem.try_get_mut<FItemInstance>();
		if (MutableInst)
		{
			MutableInst->Count -= Added;
		}
	}

	return true;
}

bool UFlecsContainerLibrary::PickupItem(
	UObject* WorldContextObject,
	FSkeletonKey WorldItemKey,
	int64 ContainerEntityId,
	int32& OutPickedUp)
{
	OutPickedUp = 0;
	if (!WorldItemKey.IsValid() || ContainerEntityId == 0) return false;

	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	Subsystem->EnqueueCommand([Subsystem, WorldItemKey, ContainerEntityId]()
	{
		flecs::entity ItemEntity = FlecsLibrary::GetEntityForKey(Subsystem, WorldItemKey);
		if (!ItemEntity.is_valid() || ItemEntity.has<FTagDead>()) return;

		int32 Picked = 0;
		PickupWorldItem(Subsystem, static_cast<int64>(ItemEntity.id()), ContainerEntityId, Picked);
	});

	return true;
}

FSkeletonKey UFlecsContainerLibrary::DropItem(
	UObject* WorldContextObject,
	int64 ContainerEntityId,
	int64 ItemEntityId,
	FVector DropLocation,
	int32 Count)
{
	if (ContainerEntityId == 0 || ItemEntityId == 0) return FSkeletonKey();

	// TODO: Implement via simulation thread
	UE_LOG(LogFlecsContainer, Warning, TEXT("DropItem: Not yet implemented"));
	return FSkeletonKey();
}

bool UFlecsContainerLibrary::TransferItem(
	UObject* WorldContextObject,
	int64 SourceContainerId,
	int64 DestContainerId,
	int64 ItemEntityId,
	FIntPoint DestGridPosition)
{
	if (SourceContainerId == 0 || DestContainerId == 0 || ItemEntityId == 0)
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("TransferItem: Invalid parameters"));
		return false;
	}

	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	Subsystem->EnqueueCommand([Subsystem, SourceContainerId, DestContainerId, ItemEntityId, DestGridPosition]()
	{
		flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		// Validate source and dest containers
		flecs::entity SrcEntity = ResolveContainerEntity(Subsystem, SourceContainerId);
		flecs::entity DstEntity = ResolveContainerEntity(Subsystem, DestContainerId);
		if (!SrcEntity.is_valid() || !DstEntity.is_valid())
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("TransferItem: Source or dest container not found"));
			return;
		}

		// Validate item
		flecs::entity ItemEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(ItemEntityId));
		if (!ItemEntity.is_alive())
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("TransferItem: Item %lld not found"), ItemEntityId);
			return;
		}

		FContainedIn* ContainedIn = ItemEntity.try_get_mut<FContainedIn>();
		if (!ContainedIn || ContainedIn->ContainerEntityId != SourceContainerId)
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("TransferItem: Item %lld not in source container %lld"),
				ItemEntityId, SourceContainerId);
			return;
		}

		const FItemStaticData* ItemStatic = ItemEntity.try_get<FItemStaticData>();
		const FIntPoint ItemSize = ItemStatic ? ItemStatic->GridSize : FIntPoint(1, 1);
		const float ItemWeight = ItemStatic ? ItemStatic->Weight : 0.f;

		FItemInstance* ItemInstance = ItemEntity.try_get_mut<FItemInstance>();
		if (!ItemInstance)
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("TransferItem: Item %lld has no FItemInstance"), ItemEntityId);
			return;
		}

		const FContainerStatic* SrcStatic = SrcEntity.try_get<FContainerStatic>();
		FContainerInstance* SrcInstance = SrcEntity.try_get_mut<FContainerInstance>();
		const FContainerStatic* DstStatic = DstEntity.try_get<FContainerStatic>();
		FContainerInstance* DstInstance = DstEntity.try_get_mut<FContainerInstance>();

		if (!SrcStatic || !SrcInstance || !DstStatic || !DstInstance)
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("TransferItem: Missing container components"));
			return;
		}

		// Weapon slot validation: only weapons can go in FTagWeaponSlot containers
		if (DstEntity.has<FTagWeaponSlot>() && !ItemEntity.has<FTagWeapon>())
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("TransferItem: Only weapons can be placed in weapon slots"));
			NotifyContainerUI(SourceContainerId);
			NotifyContainerUI(DestContainerId);
			return;
		}

		// Track if we need to unequip after successful transfer
		const bool bNeedsUnequip = SrcEntity.has<FTagWeaponSlot>() && ItemEntity.has<FEquippedBy>();

		// Weight check on dest
		const float TotalItemWeight = ItemWeight * ItemInstance->Count;
		if (DstStatic->MaxWeight >= 0.f && DstInstance->CurrentWeight + TotalItemWeight > DstStatic->MaxWeight)
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("TransferItem: Dest container %lld weight limit exceeded"), DestContainerId);
			NotifyContainerUI(SourceContainerId);
			NotifyContainerUI(DestContainerId);
			return;
		}

		// Try auto-stack in dest
		if (ItemStatic && ItemStatic->IsStackable() && DstStatic->bAutoStack)
		{
			bool bFullyStacked = false;
			FlecsWorld->each([DestContainerId, ItemStatic, ItemInstance, &bFullyStacked](
				flecs::entity E, const FContainedIn& Contained, FItemInstance& DestItemInst)
			{
				if (bFullyStacked || ItemInstance->Count <= 0) return;
				if (Contained.ContainerEntityId != DestContainerId) return;

				const FItemStaticData* DestStatic = E.try_get<FItemStaticData>();
				if (!DestStatic || DestStatic->ItemName != ItemStatic->ItemName) return;
				if (DestItemInst.Count >= DestStatic->MaxStack) return;

				const int32 Space = DestStatic->MaxStack - DestItemInst.Count;
				const int32 ToTransfer = FMath::Min(ItemInstance->Count, Space);
				DestItemInst.Count += ToTransfer;
				ItemInstance->Count -= ToTransfer;

				if (ItemInstance->Count <= 0)
				{
					bFullyStacked = true;
				}
			});

			if (bFullyStacked)
			{
				// Item fully absorbed — remove from source
				if (SrcStatic->Type == EContainerType::Grid && ContainedIn->IsInGrid())
				{
					if (FContainerGridInstance* SrcGrid = SrcEntity.try_get_mut<FContainerGridInstance>())
					{
						SrcGrid->Free(ContainedIn->GridPosition, ItemSize, SrcStatic->GridWidth);
					}
				}
				SrcInstance->CurrentCount = FMath::Max(0, SrcInstance->CurrentCount - 1);
				SrcInstance->CurrentWeight -= TotalItemWeight;

				ItemEntity.destruct();

				UE_LOG(LogFlecsContainer, Log, TEXT("TransferItem: Item %lld fully stacked into dest %lld"),
					ItemEntityId, DestContainerId);
				NotifyContainerUI(SourceContainerId);
				NotifyContainerUI(DestContainerId);
				return;
			}
		}

		// ── Free from source container ──
		if (SrcStatic->Type == EContainerType::Grid && ContainedIn->IsInGrid())
		{
			if (FContainerGridInstance* SrcGrid = SrcEntity.try_get_mut<FContainerGridInstance>())
			{
				SrcGrid->Free(ContainedIn->GridPosition, ItemSize, SrcStatic->GridWidth);
			}
		}
		else if (SrcStatic->Type == EContainerType::Slot && ContainedIn->IsInSlot())
		{
			if (FContainerSlotsInstance* SrcSlots = SrcEntity.try_get_mut<FContainerSlotsInstance>())
			{
				SrcSlots->ClearSlot(ContainedIn->SlotIndex);
			}
		}

		// ── Place in dest container ──
		FIntPoint NewGridPos = FIntPoint(-1, -1);
		int32 NewSlotIndex = -1;

		if (DstStatic->Type == EContainerType::Grid)
		{
			FContainerGridInstance* DstGrid = DstEntity.try_get_mut<FContainerGridInstance>();
			if (!DstGrid)
			{
				UE_LOG(LogFlecsContainer, Warning, TEXT("TransferItem: Dest grid container %lld has no FContainerGridInstance"), DestContainerId);
				NotifyContainerUI(SourceContainerId);
				NotifyContainerUI(DestContainerId);
				return;
			}

			if (!DstGrid->CanFit(DestGridPosition, ItemSize, DstStatic->GridWidth, DstStatic->GridHeight))
			{
				UE_LOG(LogFlecsContainer, Warning, TEXT("TransferItem: Cannot fit at (%d,%d) in dest %lld"),
					DestGridPosition.X, DestGridPosition.Y, DestContainerId);
				NotifyContainerUI(SourceContainerId);
				NotifyContainerUI(DestContainerId);
				return;
			}

			DstGrid->Occupy(DestGridPosition, ItemSize, DstStatic->GridWidth);
			NewGridPos = DestGridPosition;
		}
		else if (DstStatic->Type == EContainerType::Slot)
		{
			FContainerSlotsInstance* DstSlots = DstEntity.try_get_mut<FContainerSlotsInstance>();
			if (!DstSlots)
			{
				UE_LOG(LogFlecsContainer, Warning, TEXT("TransferItem: Dest slot container %lld has no FContainerSlotsInstance"), DestContainerId);
				NotifyContainerUI(SourceContainerId);
				NotifyContainerUI(DestContainerId);
				return;
			}

			// DestGridPosition.X is used as slot index for slot containers
			int32 TargetSlot = DestGridPosition.X;
			if (!DstSlots->IsSlotEmpty(TargetSlot))
			{
				UE_LOG(LogFlecsContainer, Warning, TEXT("TransferItem: Slot %d in container %lld is occupied"),
					TargetSlot, DestContainerId);
				NotifyContainerUI(SourceContainerId);
				NotifyContainerUI(DestContainerId);
				return;
			}

			DstSlots->SetSlot(TargetSlot, ItemEntityId);
			NewSlotIndex = TargetSlot;
		}

		// Update counts and weights
		SrcInstance->CurrentCount = FMath::Max(0, SrcInstance->CurrentCount - 1);
		SrcInstance->CurrentWeight = FMath::Max(0.f, SrcInstance->CurrentWeight - TotalItemWeight);
		DstInstance->CurrentCount++;
		DstInstance->CurrentWeight += TotalItemWeight;

		// Reparent: update FContainedIn
		ContainedIn->ContainerEntityId = DestContainerId;
		ContainedIn->GridPosition = NewGridPos;
		ContainedIn->SlotIndex = NewSlotIndex;

		// Unequip weapon AFTER successful transfer (not before — avoids unequip on failed placement)
		if (bNeedsUnequip)
		{
			const FEquippedBy* Eq = ItemEntity.try_get<FEquippedBy>();
			if (Eq && Eq->IsEquipped())
			{
				flecs::entity CharEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(Eq->CharacterEntityId));
				if (CharEntity.is_valid())
				{
					FWeaponSlotState* SlotState = CharEntity.try_get_mut<FWeaponSlotState>();
					if (SlotState)
					{
						SlotState->ActiveSlotIndex = -1;
						SlotState->PendingSlotIndex = -1;
						SlotState->EquipPhase = EWeaponEquipPhase::Idle;
						SlotState->EquipTimer = 0.f;
					}

					if (Subsystem)
						Subsystem->EnqueueWeaponEquipSignal(CharEntity, 0, -1, nullptr, nullptr, FTransform::Identity);
				}

				FWeaponInstance* WI = ItemEntity.try_get_mut<FWeaponInstance>();
				if (WI)
				{
					WI->bFireRequested = false;
					WI->bFireTriggerPending = false;
					WI->bReloadRequested = false;
				}

				ItemEntity.remove<FEquippedBy>();
				UE_LOG(LogFlecsContainer, Log, TEXT("TransferItem: Unequipped weapon %lld (moved out of weapon slot)"), ItemEntityId);
			}
		}

		UE_LOG(LogFlecsContainer, Log, TEXT("TransferItem: Moved item %lld from container %lld to %lld at (%d,%d)"),
			ItemEntityId, SourceContainerId, DestContainerId, DestGridPosition.X, DestGridPosition.Y);

		NotifyContainerUI(SourceContainerId);
		NotifyContainerUI(DestContainerId);
	});

	return true;
}

