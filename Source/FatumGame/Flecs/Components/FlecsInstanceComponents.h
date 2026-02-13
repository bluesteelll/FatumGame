// Instance components for Flecs entities.
// These components store mutable per-entity data.
// Static data comes from prefabs via IsA relationship.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "FlecsInstanceComponents.generated.h"

// ═══════════════════════════════════════════════════════════════
// INSTANCE COMPONENT ARCHITECTURE
// ═══════════════════════════════════════════════════════════════
//
// Instance components contain only MUTABLE per-entity data.
// Static/constant data comes from prefab via try_get<>.
//
// Example:
//   // Spawning entity
//   const FHealthStatic* Static = Prefab.try_get<FHealthStatic>();
//   FHealthInstance Instance;
//   Instance.CurrentHP = Static->GetStartingHP();
//   Entity.is_a(Prefab).set<FHealthInstance>(Instance);
//
//   // Taking damage
//   const FHealthStatic* Static = Entity.try_get<FHealthStatic>();  // from prefab
//   FHealthInstance* Instance = Entity.get_mut<FHealthInstance>();   // from entity
//   float EffectiveDamage = FMath::Max(0.f, Damage - Static->Armor);
//   Instance->CurrentHP -= EffectiveDamage;
// ═══════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════
// HEALTH INSTANCE
// ═══════════════════════════════════════════════════════════════

/**
 * Instance health data - mutable per-entity data.
 * Static data (MaxHP, Armor) comes from FHealthStatic in prefab.
 */
USTRUCT(BlueprintType)
struct FHealthInstance
{
	GENERATED_BODY()

	/** Current hit points */
	UPROPERTY(BlueprintReadWrite, Category = "Health")
	float CurrentHP = 100.f;

	/** Accumulated regen (for fractional regen per tick) */
	float RegenAccumulator = 0.f;

	bool IsAlive() const { return CurrentHP > 0.f; }
};

// ═══════════════════════════════════════════════════════════════
// DAMAGE EVENT SYSTEM
// ═══════════════════════════════════════════════════════════════
//
// FPendingDamage accumulates damage hits from ANY source:
// - Projectile collision
// - Abilities / Spells
// - Environment (fire, poison)
// - Fall damage
// - DoT effects
// - Direct API calls
//
// DamageObserver processes FPendingDamage via OnSet event.
// ═══════════════════════════════════════════════════════════════

/**
 * Single damage hit data.
 * NOT a USTRUCT - pure C++ for Flecs performance.
 */
struct FDamageHit
{
	/** Base damage before armor/modifiers */
	float Damage = 0.f;

	/** Source entity ID (0 = environment/no source) */
	uint64 SourceEntityId = 0;

	/** Damage type for resistances/weaknesses */
	FGameplayTag DamageType;

	/** World location of hit (for effects, knockback direction) */
	FVector HitLocation = FVector::ZeroVector;

	/** Was this a critical hit? */
	bool bIsCritical = false;

	/** Bypass armor calculation? */
	bool bIgnoreArmor = false;
};

/**
 * Pending damage component - accumulates hits for processing.
 * Added to entity when damage is queued, processed by DamageObserver.
 * NOT a USTRUCT - pure C++ for Flecs performance.
 *
 * Usage:
 *   // Queue damage from anywhere
 *   UFlecsArtillerySubsystem::Get(World)->QueueDamage(TargetEntity, 50.f, SourceId);
 *
 *   // Or manually
 *   auto* Pending = Target.get_mut<FPendingDamage>();
 *   if (!Pending) { Target.add<FPendingDamage>(); Pending = Target.get_mut<FPendingDamage>(); }
 *   Pending->Hits.Add({ 50.f, SourceId, DamageType });
 *   Target.modified<FPendingDamage>();  // Triggers observer
 */
struct FPendingDamage
{
	/** Accumulated damage hits to process */
	TArray<FDamageHit> Hits;

	/** Add a hit to pending queue */
	void AddHit(float Damage, uint64 SourceId = 0, FGameplayTag DamageType = FGameplayTag(),
	            FVector HitLocation = FVector::ZeroVector, bool bCritical = false, bool bIgnoreArmor = false)
	{
		FDamageHit Hit;
		Hit.Damage = Damage;
		Hit.SourceEntityId = SourceId;
		Hit.DamageType = DamageType;
		Hit.HitLocation = HitLocation;
		Hit.bIsCritical = bCritical;
		Hit.bIgnoreArmor = bIgnoreArmor;
		Hits.Add(Hit);
	}

	/** Clear all pending hits (keeps array capacity) */
	void Clear() { Hits.Reset(); }

	/** Check if there are pending hits */
	bool HasPendingDamage() const { return Hits.Num() > 0; }

	/** Get total pending damage (before armor) */
	float GetTotalPendingDamage() const
	{
		float Total = 0.f;
		for (const FDamageHit& Hit : Hits)
		{
			Total += Hit.Damage;
		}
		return Total;
	}
};

// ═══════════════════════════════════════════════════════════════
// PROJECTILE INSTANCE
// ═══════════════════════════════════════════════════════════════

/**
 * Instance projectile data - mutable per-entity data.
 * Static data (MaxBounces, MaxLifetime) comes from FProjectileStatic in prefab.
 */
USTRUCT(BlueprintType)
struct FProjectileInstance
{
	GENERATED_BODY()

	/** Remaining lifetime in seconds */
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	float LifetimeRemaining = 10.f;

	/** Current bounce count */
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	int32 BounceCount = 0;

	/** Grace frames remaining before velocity check */
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	int32 GraceFramesRemaining = 30;

	/** Entity that spawned this projectile (for friendly fire, damage attribution) */
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	int64 OwnerEntityId = 0;
};

// ═══════════════════════════════════════════════════════════════
// ITEM INSTANCE
// ═══════════════════════════════════════════════════════════════

/**
 * Instance item data - mutable per-entity data.
 * Static data (TypeId, MaxStack, Weight) comes from FItemStaticData in prefab.
 */
USTRUCT(BlueprintType)
struct FItemInstance
{
	GENERATED_BODY()

	/** Stack count (1 for unique items) */
	UPROPERTY(BlueprintReadWrite, Category = "Item")
	int32 Count = 1;

	bool IsValid() const { return Count > 0; }
};

/**
 * Unique item data. Only for non-stackable items (MaxStackSize == 0).
 * Stores per-instance state like durability, enchantments.
 */
USTRUCT(BlueprintType)
struct FItemUniqueData
{
	GENERATED_BODY()

	/** Current durability (0.0 - 1.0, where 1.0 = full) */
	UPROPERTY(BlueprintReadWrite, Category = "Item")
	float Durability = 1.f;

	/** Max durability for display purposes */
	UPROPERTY(BlueprintReadWrite, Category = "Item")
	float MaxDurability = 100.f;

	/** Enchantment IDs applied to this item */
	UPROPERTY(BlueprintReadWrite, Category = "Item")
	TArray<int32> EnchantmentIds;

	/** Custom key-value stats for extensibility */
	UPROPERTY(BlueprintReadWrite, Category = "Item")
	TMap<FName, float> CustomStats;

	float GetDurabilityPercent() const { return MaxDurability > 0.f ? Durability / MaxDurability : 0.f; }
	bool IsBroken() const { return Durability <= 0.f; }
};

// ═══════════════════════════════════════════════════════════════
// CONTAINER INSTANCE
// ═══════════════════════════════════════════════════════════════

/**
 * Instance container data - mutable per-entity data.
 * Static data (MaxWeight, GridSize, etc.) comes from FContainerStatic in prefab.
 */
USTRUCT(BlueprintType)
struct FContainerInstance
{
	GENERATED_BODY()

	/** Current total weight of all items */
	UPROPERTY(BlueprintReadWrite, Category = "Container")
	float CurrentWeight = 0.f;

	/** Current item count */
	UPROPERTY(BlueprintReadWrite, Category = "Container")
	int32 CurrentCount = 0;

	/** Owner entity ID (character, chest actor, etc). 0 = standalone */
	UPROPERTY(BlueprintReadOnly, Category = "Container")
	int64 OwnerEntityId = 0;

	bool HasOwner() const { return OwnerEntityId != 0; }
};

/**
 * Grid occupancy data for Grid containers.
 * Instance-specific because occupancy changes per container.
 */
USTRUCT(BlueprintType)
struct FContainerGridInstance
{
	GENERATED_BODY()

	/**
	 * Occupancy bitmap. Each bit represents one cell.
	 * Index = Y * Width + X. True = occupied.
	 * Width/Height come from FContainerStatic.
	 */
	UPROPERTY()
	TArray<uint8> OccupancyMask;

	void Initialize(int32 Width, int32 Height);
	bool IsCellOccupied(int32 X, int32 Y, int32 Width) const;
	bool CanFit(FIntPoint Position, FIntPoint Size, int32 GridWidth, int32 GridHeight) const;
	void Occupy(FIntPoint Position, FIntPoint Size, int32 GridWidth);
	void Free(FIntPoint Position, FIntPoint Size, int32 GridWidth);
	FIntPoint FindFreeSpace(FIntPoint Size, int32 GridWidth, int32 GridHeight) const;
};

/**
 * Equipment slot instance data for Slot containers.
 */
USTRUCT(BlueprintType)
struct FContainerSlotsInstance
{
	GENERATED_BODY()

	/** Item entity IDs per slot (indexed by SlotId) */
	UPROPERTY()
	TMap<int32, int64> SlotToItemEntity;

	bool IsSlotEmpty(int32 SlotId) const { return !SlotToItemEntity.Contains(SlotId) || SlotToItemEntity[SlotId] == 0; }
	int64 GetItemInSlot(int32 SlotId) const { return SlotToItemEntity.Contains(SlotId) ? SlotToItemEntity[SlotId] : 0; }
	void SetSlot(int32 SlotId, int64 ItemEntityId) { SlotToItemEntity.Add(SlotId, ItemEntityId); }
	void ClearSlot(int32 SlotId) { SlotToItemEntity.Remove(SlotId); }
};

// ═══════════════════════════════════════════════════════════════
// WORLD ITEM INSTANCE
// ═══════════════════════════════════════════════════════════════

/**
 * World item instance data (despawn timer, pickup grace).
 * For items that exist in the world with physics.
 */
USTRUCT(BlueprintType)
struct FWorldItemInstance
{
	GENERATED_BODY()

	/** Despawn timer in seconds. -1 = never despawns */
	UPROPERTY(BlueprintReadWrite, Category = "Item")
	float DespawnTimer = -1.f;

	/** Grace period after drop - can't be picked up immediately */
	UPROPERTY(BlueprintReadWrite, Category = "Item")
	float PickupGraceTimer = 0.f;

	/** Who dropped this (for ownership/grace) */
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int64 DroppedByEntityId = 0;

	bool CanBePickedUp() const { return PickupGraceTimer <= 0.f; }
};

// ═══════════════════════════════════════════════════════════════
// LOCATION / RELATIONSHIP COMPONENTS
// ═══════════════════════════════════════════════════════════════

/**
 * Marks item as contained within another entity (container).
 * Removal of this component = item is in world or nowhere.
 */
USTRUCT(BlueprintType)
struct FContainedIn
{
	GENERATED_BODY()

	/** Flecs entity ID of the container */
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int64 ContainerEntityId = 0;

	/** Grid position (for Grid containers) */
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	FIntPoint GridPosition = FIntPoint(-1, -1);

	/** Slot index (for List containers) or Slot ID (for Slot containers) */
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 SlotIndex = -1;

	bool IsInGrid() const { return GridPosition.X >= 0 && GridPosition.Y >= 0; }
	bool IsInSlot() const { return SlotIndex >= 0; }
};

// ═══════════════════════════════════════════════════════════════
// DEBRIS INSTANCE
// ═══════════════════════════════════════════════════════════════

/**
 * Instance debris data - mutable per-fragment data.
 * Present on each fragment entity spawned from a destructible object.
 * Static data (BreakForce, ImpulseMultiplier) comes from UFlecsDestructibleProfile.
 */
struct FDebrisInstance
{
	/** Remaining lifetime in seconds (only used if bAutoDestroy) */
	float LifetimeRemaining = 10.f;

	/** Should this fragment auto-destroy after lifetime expires? */
	bool bAutoDestroy = true;

	/** Index into FDebrisPool for body reuse. INDEX_NONE = not pooled. */
	int32 PoolSlotIndex = INDEX_NONE;
};

// ═══════════════════════════════════════════════════════════════
// FRAGMENTATION DATA
// ═══════════════════════════════════════════════════════════════

/**
 * Fragmentation event data — stored on collision pair entity.
 * Contains impact info needed by FragmentationSystem to apply impulse.
 * NOT a USTRUCT — pure C++ for Flecs performance.
 */
struct FFragmentationData
{
	/** World-space impact point */
	FVector ImpactPoint = FVector::ZeroVector;

	/** Impact direction (normalized) */
	FVector ImpactDirection = FVector::ZeroVector;

	/** Impact impulse magnitude */
	float ImpactImpulse = 0.f;
};
