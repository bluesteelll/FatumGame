// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "EnaceDispatch.h"
#include "EnaceModule.h"
#include "Items/EnaceItemDefinition.h"
#include "Tags/EnaceTags.h"

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

	SelfPtr = this;

	UE_LOG(LogEnace, Log, TEXT("EnaceDispatch initialized"));
}

void UEnaceDispatch::Deinitialize()
{
	if (Items) Items->clear();
	if (Health) Health->clear();
	if (Damage) Damage->clear();
	if (Loot) Loot->clear();

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

	if (ArtilleryDispatch)
	{
		ArtilleryDispatch->RemoveTagFromEntity(Key, TAG_Enace_Item);
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
