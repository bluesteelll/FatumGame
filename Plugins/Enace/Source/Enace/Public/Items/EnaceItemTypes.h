// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "SkeletonTypes.h"
#include "EnaceItemTypes.generated.h"

/**
 * Item rarity levels
 */
UENUM(BlueprintType)
enum class EEnaceItemRarity : uint8
{
	Common,
	Uncommon,
	Rare,
	Epic,
	Legendary
};

/**
 * Item category for filtering and organization
 */
UENUM(BlueprintType)
enum class EEnaceItemCategory : uint8
{
	None,
	Resource,      // Raw materials (wood, stone, ore)
	Consumable,    // Food, potions, medicine
	Equipment,     // Weapons, armor, tools
	Buildable,     // Structures, furniture
	Quest,         // Quest items
	Misc           // Everything else
};

/**
 * Lightweight item stack data (for transfers, queries)
 */
USTRUCT(BlueprintType)
struct ENACE_API FEnaceItemStack
{
	GENERATED_BODY()

	/** Item definition asset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<class UEnaceItemDefinition> Definition = nullptr;

	/** Number of items in stack */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1"))
	int32 Count = 1;

	/** Unique entity key (SkeletonKey) for tracking */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FSkeletonKey EntityKey;

	bool IsValid() const;
	bool CanStackWith(const FEnaceItemStack& Other) const;
	int32 GetMaxStackSize() const;
	int32 GetRemainingSpace() const;
};

/**
 * Result of a spawn operation
 */
USTRUCT(BlueprintType)
struct ENACE_API FEnaceSpawnResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	bool bSuccess = false;

	/** SkeletonKey for the spawned entity (use this for all operations) */
	UPROPERTY(BlueprintReadOnly)
	FSkeletonKey EntityKey;
};

/**
 * Parameters for spawning a world item
 */
USTRUCT(BlueprintType)
struct ENACE_API FEnaceWorldItemSpawnParams
{
	GENERATED_BODY()

	/** Item definition to spawn */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<class UEnaceItemDefinition> Definition = nullptr;

	/** World location */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Location = FVector::ZeroVector;

	/** World rotation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FRotator Rotation = FRotator::ZeroRotator;

	/** Number of items in stack */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1"))
	int32 Count = 1;

	/** Initial velocity (for thrown items) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector InitialVelocity = FVector::ZeroVector;

	/** Time until despawn (0 = use definition default, -1 = never) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float DespawnTime = 0.0f;

	/** Can this item be picked up immediately? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCanPickup = true;

	/** Pickup delay (prevents instant re-pickup when dropping) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
	float PickupDelay = 0.5f;
};

// NOTE: World item data is now stored in UEnaceDispatch via libcuckoo maps.
// See FEnaceItemData in EnaceDispatch.h
