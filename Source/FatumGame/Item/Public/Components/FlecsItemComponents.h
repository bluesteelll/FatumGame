// Item and Container components for Flecs Prefabs and Entities.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "FlecsGameTags.h"
#include "FlecsItemComponents.generated.h"

class UFlecsItemDefinition;
class UFlecsEntityDefinition;
class UFlecsContainerProfile;

// ═══════════════════════════════════════════════════════════════
// ITEM STATIC DATA
// ═══════════════════════════════════════════════════════════════

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

	static FItemStaticData FromProfile(UFlecsItemDefinition* ItemDef, UFlecsEntityDefinition* EntityDef = nullptr);
};

// ═══════════════════════════════════════════════════════════════
// CONTAINER STATIC
// ═══════════════════════════════════════════════════════════════

struct FContainerStatic
{
	/** Container type determines layout */
	EContainerType Type = EContainerType::List;

	/** Grid dimensions (for Grid type) */
	int32 GridWidth = 10;
	int32 GridHeight = 4;

	/** Max items (for List type, -1 = unlimited) */
	int32 MaxItems = -1;

	/** Max weight (-1 = unlimited) */
	float MaxWeight = -1.f;

	/** Allow nesting containers? */
	bool bAllowNesting = true;

	/** Auto-stack same items on add? */
	bool bAutoStack = true;

	int32 GetTotalCells() const { return GridWidth * GridHeight; }

	static FContainerStatic FromProfile(const UFlecsContainerProfile* Profile);
};

// ═══════════════════════════════════════════════════════════════
// ITEM INSTANCE
// ═══════════════════════════════════════════════════════════════

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
 */
USTRUCT(BlueprintType)
struct FItemUniqueData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Item")
	float Durability = 1.f;

	UPROPERTY(BlueprintReadWrite, Category = "Item")
	float MaxDurability = 100.f;

	UPROPERTY(BlueprintReadWrite, Category = "Item")
	TArray<int32> EnchantmentIds;

	UPROPERTY(BlueprintReadWrite, Category = "Item")
	TMap<FName, float> CustomStats;

	float GetDurabilityPercent() const { return MaxDurability > 0.f ? Durability / MaxDurability : 0.f; }
	bool IsBroken() const { return Durability <= 0.f; }
};

// ═══════════════════════════════════════════════════════════════
// CONTAINER INSTANCE
// ═══════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FContainerInstance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Container")
	float CurrentWeight = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Container")
	int32 CurrentCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Container")
	int64 OwnerEntityId = 0;

	bool HasOwner() const { return OwnerEntityId != 0; }
};

USTRUCT(BlueprintType)
struct FContainerGridInstance
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<uint8> OccupancyMask;

	void Initialize(int32 Width, int32 Height);
	bool IsCellOccupied(int32 X, int32 Y, int32 Width) const;
	bool CanFit(FIntPoint Position, FIntPoint Size, int32 GridWidth, int32 GridHeight) const;
	void Occupy(FIntPoint Position, FIntPoint Size, int32 GridWidth);
	void Free(FIntPoint Position, FIntPoint Size, int32 GridWidth);
	FIntPoint FindFreeSpace(FIntPoint Size, int32 GridWidth, int32 GridHeight) const;
};

USTRUCT(BlueprintType)
struct FContainerSlotsInstance
{
	GENERATED_BODY()

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

USTRUCT(BlueprintType)
struct FWorldItemInstance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Item")
	float DespawnTimer = -1.f;

	UPROPERTY(BlueprintReadWrite, Category = "Item")
	float PickupGraceTimer = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int64 DroppedByEntityId = 0;

	bool CanBePickedUp() const { return PickupGraceTimer <= 0.f; }
};

// ═══════════════════════════════════════════════════════════════
// CONTAINED IN (relationship)
// ═══════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FContainedIn
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int64 ContainerEntityId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	FIntPoint GridPosition = FIntPoint(-1, -1);

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 SlotIndex = -1;

	bool IsInGrid() const { return GridPosition.X >= 0 && GridPosition.Y >= 0; }
	bool IsInSlot() const { return SlotIndex >= 0; }
};

// ═══════════════════════════════════════════════════════════════
// MAGAZINE COMPONENTS
// ═══════════════════════════════════════════════════════════════

class UFlecsAmmoTypeDefinition;

/** Maximum ammo types a magazine can accept */
static constexpr int32 MAX_MAGAZINE_AMMO_TYPES = 8;

/** Maximum rounds a magazine can hold */
static constexpr int32 MAX_MAGAZINE_CAPACITY = 60;

/** Magazine static data — lives in PREFAB, shared by all magazines of this type. */
struct FMagazineStatic
{
	uint8 CaliberId = 0xFF;  // index from CaliberRegistry (0xFF = invalid)
	int32 Capacity = 30;
	float WeightPerRound = 0.011f;
	float ReloadSpeedModifier = 1.0f;

	/** Accepted ammo type definitions. Indexed by AmmoSlots values. */
	UFlecsAmmoTypeDefinition* AcceptedAmmoTypes[MAX_MAGAZINE_AMMO_TYPES] = {};
	int32 AcceptedAmmoTypeCount = 0;

	/** Check if an ammo type is accepted. Returns index (0-7) or -1. */
	int32 FindAmmoTypeIndex(const UFlecsAmmoTypeDefinition* AmmoType) const
	{
		for (int32 i = 0; i < AcceptedAmmoTypeCount; ++i)
		{
			if (AcceptedAmmoTypes[i] == AmmoType) return i;
		}
		return -1;
	}

	static FMagazineStatic FromProfile(const class UFlecsMagazineProfile* Profile, const class UFlecsCaliberRegistry* CaliberRegistry = nullptr);
};

/** Magazine instance data — per-entity mutable ammo stack.
 *  AmmoSlots stores uint8 indices into FMagazineStatic::AcceptedAmmoTypes.
 *  LIFO stack: AmmoSlots[AmmoCount-1] = next round to fire. */
struct FMagazineInstance
{
	uint8 AmmoSlots[MAX_MAGAZINE_CAPACITY] = {};
	int32 AmmoCount = 0;

	/** Pop the top round. Returns ammo type index, or -1 if empty. */
	int32 Pop()
	{
		if (AmmoCount <= 0) return -1;
		return AmmoSlots[--AmmoCount];
	}

	/** Push a round onto the stack. Returns false if full. */
	bool Push(uint8 AmmoTypeIndex)
	{
		if (AmmoCount >= MAX_MAGAZINE_CAPACITY) return false;
		AmmoSlots[AmmoCount++] = AmmoTypeIndex;
		return true;
	}

	bool IsEmpty() const { return AmmoCount <= 0; }
	bool IsFull(int32 Capacity) const { return AmmoCount >= Capacity; }
};

/** Tag: entity is a magazine */
struct FTagMagazine {};

/** Ammo type reference — on loose ammo items in inventory.
 *  Stores the ammo type index (matching FMagazineStatic::AcceptedAmmoTypes)
 *  for fast lookup during single-round reload. */
struct FAmmoTypeRef
{
	/** Index into FMagazineStatic::AcceptedAmmoTypes (-1 = unresolved, resolved lazily per-weapon) */
	int32 AmmoTypeIndex = -1;
};
