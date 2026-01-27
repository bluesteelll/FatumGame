// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// EnaceItemSpawner - Drag & drop item spawner
// Like BarrageEntitySpawner but registers item data in EnaceDispatch

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkeletonTypes.h"
#include "EnaceItemSpawner.generated.h"

class UEnaceItemDefinition;
class UEnaceDispatch;

/**
 * SIMPLE Item Spawner - drag, set ItemDefinition, play!
 *
 * HOW TO USE:
 * 1. Drag to level
 * 2. Set ItemDefinition (Data Asset)
 * 3. Set Count if needed
 * 4. Play!
 *
 * This is a wrapper around BarrageEntitySpawner that also registers
 * item data in EnaceDispatch for pickup/inventory integration.
 *
 * Physics via Jolt, rendering via ISM, data via EnaceDispatch.
 */
UCLASS(Blueprintable, BlueprintType)
class ENACE_API AEnaceItemSpawner : public AActor
{
	GENERATED_BODY()

public:
	AEnaceItemSpawner();

	// ═══════════════════════════════════════════════════════════════
	// ITEM CONFIG (just set these!)
	// ═══════════════════════════════════════════════════════════════

	/** Item definition - contains mesh, physics settings, everything */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enace Item")
	TObjectPtr<UEnaceItemDefinition> ItemDefinition;

	/** How many items in stack */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enace Item", meta = (ClampMin = "1"))
	int32 Count = 1;

	/** Initial velocity (for thrown/dropped items) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enace Item")
	FVector InitialVelocity = FVector::ZeroVector;

	/** Override despawn time (-1 = use definition default, 0 = never) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enace Item")
	float DespawnTimeOverride = -1.f;

	/** Destroy this spawner actor after item created? (recommended) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enace Item")
	bool bDestroyAfterSpawn = true;

	// ═══════════════════════════════════════════════════════════════
	// PREVIEW
	// ═══════════════════════════════════════════════════════════════

	/** Show mesh preview in editor? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	bool bShowPreview = true;

	// ═══════════════════════════════════════════════════════════════
	// API
	// ═══════════════════════════════════════════════════════════════

	/** Get the spawned item's key (valid after BeginPlay) */
	UFUNCTION(BlueprintPure, Category = "Enace Item")
	FSkeletonKey GetItemKey() const { return ItemKey; }

	/** Spawn item from Blueprint/C++ code */
	UFUNCTION(BlueprintCallable, Category = "Enace Item", meta = (WorldContext = "WorldContextObject"))
	static FSkeletonKey SpawnItem(
		UObject* WorldContextObject,
		UEnaceItemDefinition* Definition,
		FVector Location,
		int32 InCount = 1,
		FVector InVelocity = FVector(0, 0, 0)
	);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UStaticMeshComponent> PreviewMeshComponent;

	FSkeletonKey ItemKey;

	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void UpdatePreview();
#endif

private:
	FSkeletonKey DoSpawn();
};
