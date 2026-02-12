// Container snapshot types for FlecsUI.
// Plain UI data — NO ECS component types needed.
// Populated by game module's subsystem from ECS components.

#pragma once

#include "CoreMinimal.h"
#include "FlecsContainerTypes.generated.h"

/** Snapshot of a single item in a container. Game thread only. */
USTRUCT(BlueprintType)
struct FLECSUI_API FContainerItemSnapshot
{
	GENERATED_BODY()

	/** Raw Flecs entity ID (int64 for cross-thread safety). */
	UPROPERTY(BlueprintReadOnly, Category = "Container")
	int64 ItemEntityId = 0;

	/** Position in grid. */
	UPROPERTY(BlueprintReadOnly, Category = "Container")
	FIntPoint GridPosition = FIntPoint(-1, -1);

	/** Size in grid cells. */
	UPROPERTY(BlueprintReadOnly, Category = "Container")
	FIntPoint GridSize = FIntPoint(1, 1);

	/** Type identifier (for stacking comparison). */
	UPROPERTY(BlueprintReadOnly, Category = "Container")
	FName TypeId;

	/** Current stack count. */
	UPROPERTY(BlueprintReadOnly, Category = "Container")
	int32 Count = 1;

	/** Max stack size for this item type. */
	UPROPERTY(BlueprintReadOnly, Category = "Container")
	int32 MaxStack = 99;

	/** Weight per unit. */
	UPROPERTY(BlueprintReadOnly, Category = "Container")
	float Weight = 0.f;

	/** Rarity tier (for UI coloring). */
	UPROPERTY(BlueprintReadOnly, Category = "Container")
	int32 RarityTier = 0;

	/** Item definition (UDataAsset*). Plugin doesn't know concrete type — game module sets it. */
	UPROPERTY(BlueprintReadOnly, Category = "Container")
	TObjectPtr<UDataAsset> ItemDefinition;
};

/** Full container snapshot. Written to triple buffer by sim thread, read by game thread. */
USTRUCT(BlueprintType)
struct FLECSUI_API FContainerSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Container")
	int64 ContainerEntityId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Container")
	int32 GridWidth = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Container")
	int32 GridHeight = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Container")
	float MaxWeight = -1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Container")
	float CurrentWeight = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Container")
	int32 CurrentCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Container")
	TArray<FContainerItemSnapshot> Items;
};
