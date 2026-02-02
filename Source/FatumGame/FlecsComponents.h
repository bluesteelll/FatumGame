// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Flecs ECS components for FatumGame.

#pragma once

#include "CoreMinimal.h"
#include "SkeletonTypes.h"
#include "GameplayTagContainer.h"
#include "FlecsComponents.generated.h"

class UStaticMesh;
class UPrimaryDataAsset;

// ═══════════════════════════════════════════════════════════════
// CORE GAMEPLAY COMPONENTS
// ═══════════════════════════════════════════════════════════════

/** Item data for world entities. Entities with this component are items. */
USTRUCT(BlueprintType)
struct FItemData
{
	GENERATED_BODY()

	/** Item definition asset (UPrimaryDataAsset) */
	UPROPERTY(BlueprintReadWrite, Category = "Item")
	TObjectPtr<UPrimaryDataAsset> Definition;

	/** Stack count */
	UPROPERTY(BlueprintReadWrite, Category = "Item")
	int32 Count = 1;

	/** Despawn timer in seconds. -1 = never despawns. Decremented each tick. */
	UPROPERTY(BlueprintReadWrite, Category = "Item")
	float DespawnTimer = -1.f;

	bool IsValid() const { return Definition != nullptr && Count > 0; }
};

/** Health data for entities that can take damage. */
USTRUCT(BlueprintType)
struct FHealthData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Health")
	float CurrentHP = 100.f;

	UPROPERTY(BlueprintReadWrite, Category = "Health")
	float MaxHP = 100.f;

	UPROPERTY(BlueprintReadWrite, Category = "Health")
	float Armor = 0.f;

	bool IsAlive() const { return CurrentHP > 0.f; }
	float GetHealthPercent() const { return MaxHP > 0.f ? CurrentHP / MaxHP : 0.f; }
};

/** Damage source data. Entities with this deal damage on contact. */
USTRUCT(BlueprintType)
struct FDamageSource
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Damage")
	float Damage = 10.f;

	UPROPERTY(BlueprintReadWrite, Category = "Damage")
	FGameplayTag DamageType;

	UPROPERTY(BlueprintReadWrite, Category = "Damage")
	bool bAreaDamage = false;

	UPROPERTY(BlueprintReadWrite, Category = "Damage")
	float AreaRadius = 0.f;
};

/** Projectile data. Entities with this are projectiles that auto-despawn. */
USTRUCT(BlueprintType)
struct FProjectileData
{
	GENERATED_BODY()

	/** Remaining lifetime in seconds. When <= 0, projectile is destroyed. */
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	float LifetimeRemaining = 10.f;

	/** Max bounces before destruction (-1 = infinite) */
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	int32 MaxBounces = -1;

	/** Current bounce count */
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	int32 BounceCount = 0;

	/**
	 * Grace period frames remaining. While > 0, velocity check is skipped.
	 * This prevents killing projectiles that momentarily slow down during bounce.
	 * Reset to GracePeriodFrames after each bounce collision.
	 */
	int32 GraceFramesRemaining = 30; // ~0.25 sec at 120Hz

	/** Number of frames to wait before checking velocity (grace period after spawn/bounce) */
	static constexpr int32 GracePeriodFrames = 30;
};

/** Loot drop data. When an entity with this and FHealthData dies, loot spawns. */
USTRUCT(BlueprintType)
struct FLootData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Loot")
	int32 MinDrops = 1;

	UPROPERTY(BlueprintReadWrite, Category = "Loot")
	int32 MaxDrops = 3;
};

// ═══════════════════════════════════════════════════════════════
// PHYSICS BRIDGE COMPONENTS (Bidirectional Lock-Free Binding)
// ═══════════════════════════════════════════════════════════════
//
// BIDIRECTIONAL BINDING ARCHITECTURE:
// ────────────────────────────────────────────────────────────────
// Forward lookup (Entity → BarrageKey):
//   - Flecs sparse set: entity.get<FBarrageBody>().BarrageKey  [O(1)]
//
// Reverse lookup (BarrageKey → Entity):
//   - libcuckoo map: KeyToFBLet[BarrageKey] → FBLet            [O(1)]
//   - atomic load:   FBLet->GetFlecsEntity()                   [O(1)]
//
// Both directions are lock-free and thread-safe for collision processing.
// ═══════════════════════════════════════════════════════════════

/**
 * Forward binding: Flecs Entity → Barrage physics body.
 * Part of bidirectional lock-free binding system.
 *
 * Usage:
 *   FSkeletonKey Key = entity.get<FBarrageBody>()->BarrageKey;
 *
 * Reverse binding (BarrageKey → Entity) is via FBarragePrimitive::GetFlecsEntity()
 */
USTRUCT(BlueprintType)
struct FBarrageBody
{
	GENERATED_BODY()

	/** SkeletonKey for the Barrage primitive. Used to look up the FBLet. */
	UPROPERTY(BlueprintReadOnly, Category = "Physics")
	FSkeletonKey BarrageKey;

	bool IsValid() const { return BarrageKey.IsValid(); }
};

/** ISM (Instanced Static Mesh) rendering data. Entities with this are rendered via UBarrageRenderManager. */
USTRUCT(BlueprintType)
struct FISMRender
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Render")
	TObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(BlueprintReadWrite, Category = "Render")
	FVector Scale = FVector::OneVector;
};

// ═══════════════════════════════════════════════════════════════
// CONTAINER COMPONENTS
// ═══════════════════════════════════════════════════════════════

/** A single slot within a container. */
USTRUCT(BlueprintType)
struct FContainerSlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Container")
	TObjectPtr<UPrimaryDataAsset> ItemDefinition;

	UPROPERTY(BlueprintReadWrite, Category = "Container")
	int32 Count = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Container")
	FGameplayTag SlotTypeFilter;

	UPROPERTY(BlueprintReadWrite, Category = "Container")
	bool bIsLocked = false;

	bool IsEmpty() const { return ItemDefinition == nullptr || Count <= 0; }
	void Clear() { ItemDefinition = nullptr; Count = 0; }
};

/** Container data (inventory, chest, bag). Entities with this can hold items. */
USTRUCT(BlueprintType)
struct FContainerData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Container")
	TArray<FContainerSlot> Slots;

	UPROPERTY(BlueprintReadWrite, Category = "Container")
	int32 MaxSlots = -1; // -1 = dynamic

	UPROPERTY(BlueprintReadWrite, Category = "Container")
	bool bAllowNesting = true;

	int32 GetUsedSlotCount() const;
	int32 GetEmptySlotCount() const;
	bool IsFull() const;
	int32 FindFirstEmptySlot() const;
};

// ═══════════════════════════════════════════════════════════════
// ADVANCED ITEM SYSTEM COMPONENTS
// ═══════════════════════════════════════════════════════════════
//
// ITEM PREFAB ARCHITECTURE:
// ────────────────────────────────────────────────────────────────
// Static data lives in PREFAB entity (shared, one per item type):
//   - FItemStaticData: TypeId, MaxStack, Weight, EntityDefinition ptr
//
// Instance data lives in ITEM entity (per-item):
//   - FItemInstance: Count only
//   - FItemUniqueData: Durability, enchants (for unique items)
//
// Usage:
//   flecs::entity Item = world.entity()
//       .is_a(ItemPrefab)              // inherits FItemStaticData
//       .set<FItemInstance>({5});      // instance: 5 items in stack
//
// Lookup:
//   auto* Static = Item.get<FItemStaticData>();  // from prefab (shared)
//   auto* Instance = Item.get<FItemInstance>();   // from entity (own)
// ═══════════════════════════════════════════════════════════════

class UFlecsItemDefinition;
class UFlecsEntityDefinition;

/** Container type enum */
UENUM(BlueprintType)
enum class EContainerType : uint8
{
	Grid	UMETA(DisplayName = "Grid (2D inventory)"),
	Slot	UMETA(DisplayName = "Slot (Named equipment)"),
	List	UMETA(DisplayName = "List (Simple array)")
};

/** Result of item operations */
UENUM(BlueprintType)
enum class EItemResult : uint8
{
	Success,
	PartialSuccess,		// Only some items added (stack overflow)
	ContainerFull,
	InvalidItem,
	InvalidContainer,
	SlotOccupied,
	ItemTooLarge,		// Grid: item doesn't fit
	FilterRejected,		// Slot filter doesn't allow this item
	NotStackable
};

/**
 * STATIC item data - lives in PREFAB, shared by all instances of this item type.
 * One prefab per UFlecsEntityDefinition with ItemDefinition.
 *
 * Contains immutable data that never changes per-instance:
 * - Item type identification
 * - Stacking rules
 * - Weight, grid size
 * - Reference back to EntityDefinition (for spawning in world)
 */
struct FItemStaticData
{
	/** Item type ID (from UFlecsItemDefinition::ItemTypeId) */
	int32 TypeId = 0;

	/** Max stack size (0 = unique, 1 = single, >1 = stackable) */
	int32 MaxStack = 99;

	/** Item weight per unit */
	float Weight = 0.1f;

	/** Grid size for 2D inventories */
	FIntPoint GridSize = FIntPoint(1, 1);

	/** Item name for debug/display */
	FName ItemName;

	/**
	 * Reference to the full EntityDefinition.
	 * Used when dropping item to world (needs physics/render profiles).
	 * Stored as raw pointer - prefabs outlive items, Definition outlives prefab.
	 */
	UFlecsEntityDefinition* EntityDefinition = nullptr;

	/** Reference to ItemDefinition for gameplay data (actions, tags, etc) */
	UFlecsItemDefinition* ItemDefinition = nullptr;

	bool IsStackable() const { return MaxStack > 1; }
	bool IsUnique() const { return MaxStack == 0; }
};

/**
 * INSTANCE item data - lives on each item entity (not in prefab).
 * Contains mutable per-instance data.
 *
 * Static data (TypeId, MaxStack, Weight) comes from prefab via IsA.
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

/**
 * World item specific data (despawn, physics state).
 * Combined with FBarrageBody for physics.
 */
USTRUCT(BlueprintType)
struct FWorldItemData
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

/**
 * Base container component. All containers have this.
 */
USTRUCT(BlueprintType)
struct FContainerBase
{
	GENERATED_BODY()

	/** Container type determines layout */
	UPROPERTY(BlueprintReadOnly, Category = "Container")
	EContainerType Type = EContainerType::List;

	/** Definition ID for lookup (0 = dynamic/runtime created) */
	UPROPERTY(BlueprintReadOnly, Category = "Container")
	int32 DefinitionId = 0;

	/** Owner entity (character, chest actor, etc). 0 = standalone */
	UPROPERTY(BlueprintReadOnly, Category = "Container")
	int64 OwnerEntityId = 0;

	/** Total weight of all items */
	UPROPERTY(BlueprintReadWrite, Category = "Container")
	float CurrentWeight = 0.f;

	/** Max weight (-1 = unlimited) */
	UPROPERTY(BlueprintReadWrite, Category = "Container")
	float MaxWeight = -1.f;

	bool HasOwner() const { return OwnerEntityId != 0; }
	bool IsOverweight() const { return MaxWeight > 0.f && CurrentWeight > MaxWeight; }
};

/**
 * Grid-based container (2D inventory like Diablo/Tarkov).
 * Items occupy WxH cells based on their GridSize.
 */
USTRUCT(BlueprintType)
struct FContainerGridData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Container")
	int32 Width = 10;

	UPROPERTY(BlueprintReadOnly, Category = "Container")
	int32 Height = 4;

	/**
	 * Occupancy bitmap. Each bit represents one cell.
	 * Index = Y * Width + X. True = occupied.
	 */
	UPROPERTY()
	TArray<uint8> OccupancyMask;

	void Initialize(int32 InWidth, int32 InHeight);
	bool CanFit(FIntPoint Position, FIntPoint Size) const;
	void Occupy(FIntPoint Position, FIntPoint Size);
	void Free(FIntPoint Position, FIntPoint Size);
	FIntPoint FindFreeSpace(FIntPoint Size) const;
	int32 GetCellIndex(int32 X, int32 Y) const { return Y * Width + X; }
	bool IsCellOccupied(int32 X, int32 Y) const;
};

/**
 * Equipment slot for slot-based containers (Head, Chest, Weapon, etc).
 * Renamed from FNamedSlot to avoid conflict with UMG's UNamedSlot.
 */
USTRUCT(BlueprintType)
struct FEquipmentSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slot")
	FName SlotName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slot")
	int32 SlotId = 0;

	/** Item entity in this slot (0 = empty) */
	UPROPERTY(BlueprintReadOnly, Category = "Slot")
	int64 ItemEntityId = 0;

	/** Filter: only items with matching tag allowed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slot")
	FGameplayTag SlotFilter;

	bool IsEmpty() const { return ItemEntityId == 0; }
};

USTRUCT(BlueprintType)
struct FContainerSlotsData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Container")
	TArray<FEquipmentSlot> Slots;

	FEquipmentSlot* FindSlotById(int32 SlotId);
	const FEquipmentSlot* FindSlotById(int32 SlotId) const;
	FEquipmentSlot* FindSlotByName(FName Name);
	int32 FindEmptySlotId() const;
	bool IsSlotEmpty(int32 SlotId) const;
};

/**
 * Simple list container (no positioning).
 */
USTRUCT(BlueprintType)
struct FContainerListData
{
	GENERATED_BODY()

	/** Max items (-1 = unlimited) */
	UPROPERTY(BlueprintReadWrite, Category = "Container")
	int32 MaxItems = -1;

	/** Current item count (tracked for fast queries) */
	UPROPERTY(BlueprintReadOnly, Category = "Container")
	int32 CurrentCount = 0;

	bool IsFull() const { return MaxItems > 0 && CurrentCount >= MaxItems; }
	bool CanAdd() const { return MaxItems < 0 || CurrentCount < MaxItems; }
};

// ═══════════════════════════════════════════════════════════════
// ECS TAGS (zero-size components for archetype filtering)
// These are not USTRUCT - they are pure C++ Flecs tags.
// ═══════════════════════════════════════════════════════════════

/** Entity is a world item (has physics, can be picked up) */
struct FTagItem {};

/** Entity is a world item that was just dropped (pickup grace period) */
struct FTagDroppedItem {};

/** Entity is a container */
struct FTagContainer {};

/** Entity can be destroyed by damage */
struct FTagDestructible {};

/** Entity can be picked up by players */
struct FTagPickupable {};

/** Entity has loot drops on death */
struct FTagHasLoot {};

/** Entity is dead (pending cleanup) */
struct FTagDead {};

/** Entity is a projectile that deals damage on contact */
struct FTagProjectile {};

/** Entity is a character/player */
struct FTagCharacter {};

/** Entity is equipment (can be equipped to slot) */
struct FTagEquipment {};

/** Entity is consumable (can be used/eaten) */
struct FTagConsumable {};

// ═══════════════════════════════════════════════════════════════
// COLLISION COMPONENTS
// ═══════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════
// CONSTRAINT COMPONENTS
// ═══════════════════════════════════════════════════════════════

/** Single constraint link data. */
USTRUCT(BlueprintType)
struct FConstraintLink
{
	GENERATED_BODY()

	/** Constraint key from Barrage ConstraintSystem */
	UPROPERTY(BlueprintReadOnly, Category = "Constraint")
	int64 ConstraintKey = 0;

	/** SkeletonKey of the other entity in this constraint */
	UPROPERTY(BlueprintReadOnly, Category = "Constraint")
	FSkeletonKey OtherEntityKey;

	/** Break force threshold (0 = unbreakable) */
	UPROPERTY(BlueprintReadOnly, Category = "Constraint")
	float BreakForce = 0.f;

	/** Break torque threshold (0 = unbreakable) */
	UPROPERTY(BlueprintReadOnly, Category = "Constraint")
	float BreakTorque = 0.f;

	bool IsValid() const { return ConstraintKey != 0; }
};

/** Constraint data. Entities with this are constrained to other entities. */
USTRUCT(BlueprintType)
struct FFlecsConstraintData
{
	GENERATED_BODY()

	/** All constraints this entity participates in */
	UPROPERTY(BlueprintReadOnly, Category = "Constraint")
	TArray<FConstraintLink> Constraints;

	int32 GetConstraintCount() const { return Constraints.Num(); }
	bool HasConstraints() const { return Constraints.Num() > 0; }

	void AddConstraint(int64 Key, FSkeletonKey OtherKey, float BreakForce = 0.f, float BreakTorque = 0.f)
	{
		FConstraintLink Link;
		Link.ConstraintKey = Key;
		Link.OtherEntityKey = OtherKey;
		Link.BreakForce = BreakForce;
		Link.BreakTorque = BreakTorque;
		Constraints.Add(Link);
	}

	bool RemoveConstraint(int64 Key)
	{
		return Constraints.RemoveAll([Key](const FConstraintLink& L) { return L.ConstraintKey == Key; }) > 0;
	}
};

/** Tag for entities that are part of a constraint chain (optimization for queries) */
struct FTagConstrained {};

// ═══════════════════════════════════════════════════════════════
// COLLISION COMPONENTS
// ═══════════════════════════════════════════════════════════════

/**
 * Collision event data. Created when two Flecs-tracked entities collide.
 * Processed by collision systems, then removed.
 */
USTRUCT(BlueprintType)
struct FFlecsCollisionEvent
{
	GENERATED_BODY()

	/** SkeletonKey of the other entity in the collision */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	FSkeletonKey OtherKey;

	/** Flecs entity ID of the other entity (0 if not tracked by Flecs) */
	uint64 OtherFlecsId = 0;

	/** World-space contact point */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	FVector ContactPoint = FVector::ZeroVector;

	/** Is the other entity a projectile? */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	bool bOtherIsProjectile = false;

	/** Is the other entity static geometry? */
	UPROPERTY(BlueprintReadOnly, Category = "Collision")
	bool bOtherIsStatic = false;
};
