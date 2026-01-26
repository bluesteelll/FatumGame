// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SkeletonTypes.h"
#include "ORDIN.h"
#include "GameplayTagContainer.h"

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

	// Cached references
	UBarrageDispatch* BarrageDispatch = nullptr;
	UArtilleryDispatch* ArtilleryDispatch = nullptr;

	// Key generation
	std::atomic<uint32> KeyCounter{1};
	FSkeletonKey GenerateItemKey();
};
