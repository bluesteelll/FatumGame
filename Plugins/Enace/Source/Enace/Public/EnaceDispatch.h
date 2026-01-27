// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SkeletonTypes.h"
#include "ORDIN.h"
#include "GameplayTagContainer.h"
#include "Containers/EnaceContainerTypes.h"

THIRD_PARTY_INCLUDES_START
PRAGMA_PUSH_PLATFORM_DEFAULT_PACKING
#include "libcuckoo/cuckoohash_map.hh"
PRAGMA_POP_PLATFORM_DEFAULT_PACKING
THIRD_PARTY_INCLUDES_END

#include "EnaceDispatch.generated.h"

class UEnaceItemDefinition;
class UBarrageDispatch;
class UArtilleryDispatch;
class UBarrageRenderManager;

// ═══════════════════════════════════════════════════════════════════════════
// DATA TYPES
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Data for an item in the world.
 */
USTRUCT(BlueprintType)
struct ENACE_API FEnaceItemData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UEnaceItemDefinition> Definition = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Count = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float DespawnTimer = -1.f;  // -1 = never despawns

	bool IsValid() const { return Definition != nullptr && Count > 0; }
};

/**
 * Health data for entities that can take damage.
 */
USTRUCT(BlueprintType)
struct ENACE_API FEnaceHealthData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CurrentHP = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxHP = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Armor = 0.f;

	bool IsAlive() const { return CurrentHP > 0.f; }
	float GetHealthPercent() const { return MaxHP > 0.f ? CurrentHP / MaxHP : 0.f; }
};

/**
 * Damage data for projectiles, traps, hazards.
 */
USTRUCT(BlueprintType)
struct ENACE_API FEnaceDamageData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Damage = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag DamageType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bAreaDamage = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float AreaRadius = 0.f;
};

/**
 * Loot drop data for entities.
 */
USTRUCT(BlueprintType)
struct ENACE_API FEnaceLootData
{
	GENERATED_BODY()

	// TODO: Add loot table reference when implemented
	// UPROPERTY(EditAnywhere, BlueprintReadWrite)
	// TObjectPtr<UEnaceLootTable> LootTable = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 MinDrops = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 MaxDrops = 3;
};

// Type aliases for libcuckoo maps
typedef libcuckoo::cuckoohash_map<FSkeletonKey, FEnaceItemData> ItemDataMap;
typedef libcuckoo::cuckoohash_map<FSkeletonKey, FEnaceHealthData> HealthDataMap;
typedef libcuckoo::cuckoohash_map<FSkeletonKey, FEnaceDamageData> DamageDataMap;
typedef libcuckoo::cuckoohash_map<FSkeletonKey, FEnaceLootData> LootDataMap;
typedef libcuckoo::cuckoohash_map<FSkeletonKey, FEnaceContainerData> ContainerDataMap;

// ═══════════════════════════════════════════════════════════════════════════
// ENACE DISPATCH
// ═══════════════════════════════════════════════════════════════════════════

/**
 * EnaceDispatch - Gameplay Data Storage System
 *
 * Thread-safe storage for structured gameplay data indexed by SkeletonKey.
 * Follows the same patterns as TransformDispatch and BarrageDispatch.
 *
 * Use this for:
 * - Item data (definition, count, despawn timer)
 * - Health data (HP, armor)
 * - Damage data (for projectiles, traps)
 * - Loot data (drop tables)
 *
 * Thread Safety:
 * - Uses libcuckoo concurrent hash maps
 * - Safe to read/write from Artillery thread without locks
 */
UCLASS()
class ENACE_API UEnaceDispatch : public UWorldSubsystem, public ISkeletonLord, public ICanReady
{
	GENERATED_BODY()

public:
	static inline UEnaceDispatch* SelfPtr = nullptr;
	constexpr static int OrdinateSeqKey = ORDIN::ArtilleryOnline + ORDIN::Step;  // After Artillery

	/** Convenience getter */
	static UEnaceDispatch* Get(UWorld* World)
	{
		return World ? World->GetSubsystem<UEnaceDispatch>() : nullptr;
	}

	// ═══════════════════════════════════════════════════════════════
	// ITEMS
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Spawn an item in the world with physics and rendering.
	 * Creates a Barrage physics body and registers with UBarrageRenderManager.
	 *
	 * @param Definition Item definition asset
	 * @param Location World location
	 * @param Count Number of items in stack
	 * @param InitialVelocity Optional initial velocity
	 * @return SkeletonKey for the spawned item (invalid if failed)
	 */
	UFUNCTION(BlueprintCallable, Category = "Enace|Items")
	FSkeletonKey SpawnWorldItem(UEnaceItemDefinition* Definition, FVector Location, int32 Count = 1, FVector InitialVelocity = FVector::ZeroVector);

	/** Check if a key represents an item */
	UFUNCTION(BlueprintPure, Category = "Enace|Items")
	bool IsItem(FSkeletonKey Key) const;

	/** Try to get item data for a key */
	UFUNCTION(BlueprintPure, Category = "Enace|Items")
	bool TryGetItemData(FSkeletonKey Key, FEnaceItemData& OutData) const;

	/** Set item count (destroys item if count <= 0) */
	UFUNCTION(BlueprintCallable, Category = "Enace|Items")
	void SetItemCount(FSkeletonKey Key, int32 NewCount);

	/** Destroy an item (physics body + render instance + data) */
	UFUNCTION(BlueprintCallable, Category = "Enace|Items")
	void DestroyItem(FSkeletonKey Key);

	// ═══════════════════════════════════════════════════════════════
	// HEALTH
	// ═══════════════════════════════════════════════════════════════

	/** Register health for an entity */
	UFUNCTION(BlueprintCallable, Category = "Enace|Health")
	void RegisterHealth(FSkeletonKey Key, float MaxHP, float CurrentHP = -1.f);

	/** Try to get health data for a key */
	UFUNCTION(BlueprintPure, Category = "Enace|Health")
	bool TryGetHealthData(FSkeletonKey Key, FEnaceHealthData& OutData) const;

	/**
	 * Apply damage to an entity.
	 * @param Key Entity to damage
	 * @param DamageAmount Raw damage amount (armor is subtracted)
	 * @param Instigator Optional key of damage source
	 * @return True if the entity was killed
	 */
	UFUNCTION(BlueprintCallable, Category = "Enace|Health")
	bool ApplyDamage(FSkeletonKey Key, float DamageAmount, FSkeletonKey Instigator = FSkeletonKey());

	/** Heal an entity */
	UFUNCTION(BlueprintCallable, Category = "Enace|Health")
	bool Heal(FSkeletonKey Key, float Amount);

	/** Check if entity is alive */
	UFUNCTION(BlueprintPure, Category = "Enace|Health")
	bool IsAlive(FSkeletonKey Key) const;

	// ═══════════════════════════════════════════════════════════════
	// DAMAGE SOURCE
	// ═══════════════════════════════════════════════════════════════

	/** Register damage data for an entity (projectile, trap, etc.) */
	UFUNCTION(BlueprintCallable, Category = "Enace|Damage")
	void RegisterDamageSource(FSkeletonKey Key, const FEnaceDamageData& Data);

	/** Try to get damage data for a key */
	UFUNCTION(BlueprintPure, Category = "Enace|Damage")
	bool TryGetDamageData(FSkeletonKey Key, FEnaceDamageData& OutData) const;

	// ═══════════════════════════════════════════════════════════════
	// LOOT
	// ═══════════════════════════════════════════════════════════════

	/** Register loot data for an entity */
	UFUNCTION(BlueprintCallable, Category = "Enace|Loot")
	void RegisterLoot(FSkeletonKey Key, const FEnaceLootData& Data);

	/** Try to get loot data for a key */
	UFUNCTION(BlueprintPure, Category = "Enace|Loot")
	bool TryGetLootData(FSkeletonKey Key, FEnaceLootData& OutData) const;

	/** Spawn loot drops at a location (removes loot data after spawning) */
	UFUNCTION(BlueprintCallable, Category = "Enace|Loot")
	void SpawnLoot(FSkeletonKey Key, FVector Location);

	// ═══════════════════════════════════════════════════════════════
	// CONTAINERS
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Create a new container.
	 *
	 * @param Definition Optional item definition (for container items like bags/chests)
	 * @param SlotCount Number of slots (-1 for dynamic)
	 * @param Owner Optional owner key (actor, item)
	 * @return SkeletonKey for the container
	 */
	UFUNCTION(BlueprintCallable, Category = "Enace|Containers")
	FSkeletonKey CreateContainer(UEnaceItemDefinition* Definition, int32 SlotCount = 10, FSkeletonKey Owner = FSkeletonKey());

	/**
	 * Create a container from an item definition that has bIsContainer=true.
	 * Uses ContainerCapacity from the definition.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enace|Containers")
	FSkeletonKey CreateContainerFromDefinition(UEnaceItemDefinition* Definition, FSkeletonKey Owner = FSkeletonKey());

	/** Destroy a container and all its contents */
	UFUNCTION(BlueprintCallable, Category = "Enace|Containers")
	void DestroyContainer(FSkeletonKey ContainerKey);

	/** Check if a key represents a container */
	UFUNCTION(BlueprintPure, Category = "Enace|Containers")
	bool IsContainer(FSkeletonKey Key) const;

	/** Try to get container data */
	UFUNCTION(BlueprintPure, Category = "Enace|Containers")
	bool TryGetContainerData(FSkeletonKey Key, FEnaceContainerData& OutData) const;

	/**
	 * Find first world container (for testing).
	 * Returns the key of the first container found in the world (not player inventories).
	 */
	UFUNCTION(BlueprintPure, Category = "Enace|Containers")
	FSkeletonKey FindFirstWorldContainer() const;

	/**
	 * Get all world container keys.
	 * Excludes containers owned by players (OwnerKey is set).
	 */
	UFUNCTION(BlueprintPure, Category = "Enace|Containers")
	TArray<FSkeletonKey> GetAllWorldContainers() const;

	// ─────────────────────────────────────────────────────────────────
	// Container Slot Operations
	// ─────────────────────────────────────────────────────────────────

	/**
	 * Add an item to a container (auto-finds best slot, handles stacking).
	 *
	 * @param ContainerKey Container to add to
	 * @param Definition Item to add
	 * @param Count Number of items
	 * @return Result with success status, added count, and overflow
	 */
	UFUNCTION(BlueprintCallable, Category = "Enace|Containers")
	FEnaceAddItemResult AddItem(FSkeletonKey ContainerKey, UEnaceItemDefinition* Definition, int32 Count = 1);

	/**
	 * Add an item to a specific slot.
	 * Handles stacking if slot has same item type.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enace|Containers")
	FEnaceAddItemResult AddItemToSlot(FSkeletonKey ContainerKey, int32 SlotIndex, UEnaceItemDefinition* Definition, int32 Count = 1);

	/**
	 * Remove items from a specific slot.
	 *
	 * @param ContainerKey Container to remove from
	 * @param SlotIndex Slot index
	 * @param Count Number to remove (-1 = all)
	 * @param OutDefinition Receives the item definition
	 * @param OutCount Receives actual count removed
	 * @return True if items were removed
	 */
	UFUNCTION(BlueprintCallable, Category = "Enace|Containers")
	bool RemoveItem(FSkeletonKey ContainerKey, int32 SlotIndex, int32 Count, UEnaceItemDefinition*& OutDefinition, int32& OutCount);

	/**
	 * Move items between containers/slots.
	 *
	 * @param FromContainer Source container
	 * @param FromSlot Source slot
	 * @param ToContainer Destination container (can be same as source)
	 * @param ToSlot Destination slot
	 * @param Count Items to move (-1 = all)
	 * @return Result with success status
	 */
	UFUNCTION(BlueprintCallable, Category = "Enace|Containers")
	FEnaceMoveItemResult MoveItem(FSkeletonKey FromContainer, int32 FromSlot, FSkeletonKey ToContainer, int32 ToSlot, int32 Count = -1);

	/**
	 * Swap two slots (same or different containers).
	 */
	UFUNCTION(BlueprintCallable, Category = "Enace|Containers")
	bool SwapSlots(FSkeletonKey ContainerA, int32 SlotA, FSkeletonKey ContainerB, int32 SlotB);

	// ─────────────────────────────────────────────────────────────────
	// Container Queries
	// ─────────────────────────────────────────────────────────────────

	/** Get a specific slot */
	UFUNCTION(BlueprintPure, Category = "Enace|Containers")
	bool TryGetSlot(FSkeletonKey ContainerKey, int32 SlotIndex, FEnaceContainerSlot& OutSlot) const;

	/** Get number of slots in a container */
	UFUNCTION(BlueprintPure, Category = "Enace|Containers")
	int32 GetSlotCount(FSkeletonKey ContainerKey) const;

	/** Get number of used (non-empty) slots */
	UFUNCTION(BlueprintPure, Category = "Enace|Containers")
	int32 GetUsedSlotCount(FSkeletonKey ContainerKey) const;

	/** Find first empty slot (-1 if none or full) */
	UFUNCTION(BlueprintPure, Category = "Enace|Containers")
	int32 FindFirstEmptySlot(FSkeletonKey ContainerKey) const;

	/** Find slot containing specific item type (-1 if not found) */
	UFUNCTION(BlueprintPure, Category = "Enace|Containers")
	int32 FindItemSlot(FSkeletonKey ContainerKey, UEnaceItemDefinition* Definition) const;

	/** Get total count of a specific item across all slots */
	UFUNCTION(BlueprintPure, Category = "Enace|Containers")
	int32 GetItemCount(FSkeletonKey ContainerKey, UEnaceItemDefinition* Definition) const;

	/** Check if container has at least N of an item */
	UFUNCTION(BlueprintPure, Category = "Enace|Containers")
	bool HasItem(FSkeletonKey ContainerKey, UEnaceItemDefinition* Definition, int32 MinCount = 1) const;

	// ─────────────────────────────────────────────────────────────────
	// Container Slot Configuration
	// ─────────────────────────────────────────────────────────────────

	/** Set slot type filter (restrict what items can go in slot) */
	UFUNCTION(BlueprintCallable, Category = "Enace|Containers")
	bool SetSlotTypeFilter(FSkeletonKey ContainerKey, int32 SlotIndex, FGameplayTag TypeFilter);

	/** Lock/unlock a slot */
	UFUNCTION(BlueprintCallable, Category = "Enace|Containers")
	bool SetSlotLocked(FSkeletonKey ContainerKey, int32 SlotIndex, bool bLocked);

	/** Add slots to a dynamic container */
	UFUNCTION(BlueprintCallable, Category = "Enace|Containers")
	bool AddSlots(FSkeletonKey ContainerKey, int32 Count);

	/** Remove empty slots from end of container */
	UFUNCTION(BlueprintCallable, Category = "Enace|Containers")
	bool RemoveEmptySlots(FSkeletonKey ContainerKey, int32 Count);

	// ─────────────────────────────────────────────────────────────────
	// Container Events
	// ─────────────────────────────────────────────────────────────────

	/** Event fired when container contents change (game thread safe) */
	UPROPERTY(BlueprintAssignable, Category = "Enace|Containers")
	FOnEnaceContainerChanged OnContainerChanged;

	/** Native delegate for C++ (game thread safe) */
	FOnEnaceContainerChangedNative OnContainerChangedNative;

	// ═══════════════════════════════════════════════════════════════
	// CLEANUP
	// ═══════════════════════════════════════════════════════════════

	/** Unregister ALL data for a key (call when destroying an entity) */
	UFUNCTION(BlueprintCallable, Category = "Enace")
	void UnregisterAll(FSkeletonKey Key);

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool RegistrationImplementation() override;

private:
	// Thread-safe storage (libcuckoo)
	TSharedPtr<ItemDataMap> Items;
	TSharedPtr<HealthDataMap> Health;
	TSharedPtr<DamageDataMap> Damage;
	TSharedPtr<LootDataMap> Loot;
	TSharedPtr<ContainerDataMap> Containers;

	// Cached references
	UBarrageDispatch* BarrageDispatch = nullptr;
	UArtilleryDispatch* ArtilleryDispatch = nullptr;

	// Key generation
	std::atomic<uint32> KeyCounter{1};
	FSkeletonKey GenerateItemKey();
	FSkeletonKey GenerateContainerKey();

	// Internal helpers
	void BroadcastContainerEvent(const FEnaceContainerEvent& Event);
	bool ValidateSlotIndex(const FEnaceContainerData& Data, int32 SlotIndex) const;
	bool CanPlaceInSlot(const FEnaceContainerSlot& Slot, const UEnaceItemDefinition* Definition, const FEnaceContainerData& ContainerData) const;
};
