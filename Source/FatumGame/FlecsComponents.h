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
// PHYSICS BRIDGE COMPONENTS
// ═══════════════════════════════════════════════════════════════

/** Links a Flecs entity to its Barrage (Jolt) physics body via SkeletonKey. */
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
// ECS TAGS (zero-size components for archetype filtering)
// These are not USTRUCT - they are pure C++ Flecs tags.
// ═══════════════════════════════════════════════════════════════

/** Entity is a world item */
struct FTagItem {};

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
