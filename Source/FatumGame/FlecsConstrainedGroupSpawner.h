// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Actor-based spawner for groups of constrained Flecs entities.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlecsConstrainedGroupDefinition.h"
#include "FlecsConstrainedGroupSpawner.generated.h"

class UBillboardComponent;

/**
 * Drag-and-drop spawner for constrained entity groups.
 *
 * HOW TO USE:
 * 1. Drag to level
 * 2. Add Elements (meshes with transforms)
 * 3. Add Constraints (links between elements)
 * 4. Play!
 *
 * All entities share draw calls via ISM. Physics via Jolt.
 * Constraints can break under force.
 */
UCLASS(Blueprintable, BlueprintType)
class FATUMGAME_API AFlecsConstrainedGroupSpawner : public AActor
{
	GENERATED_BODY()

public:
	AFlecsConstrainedGroupSpawner();

	// ═══════════════════════════════════════════════════════════════
	// ELEMENTS
	// ═══════════════════════════════════════════════════════════════

	/** Elements in this group (each becomes a physics body) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constrained Group")
	TArray<FFlecsGroupElement> Elements;

	// ═══════════════════════════════════════════════════════════════
	// CONSTRAINTS
	// ═══════════════════════════════════════════════════════════════

	/** Constraints between elements */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constrained Group")
	TArray<FFlecsGroupConstraint> Constraints;

	// ═══════════════════════════════════════════════════════════════
	// BEHAVIOR
	// ═══════════════════════════════════════════════════════════════

	/** Destroy spawner actor after entities created? (recommended) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constrained Group")
	bool bDestroyAfterSpawn = true;

	/** Spawn on BeginPlay? If false, call SpawnGroup() manually */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constrained Group")
	bool bSpawnOnBeginPlay = true;

	// ═══════════════════════════════════════════════════════════════
	// PREVIEW
	// ═══════════════════════════════════════════════════════════════

	/** Show element previews in editor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	bool bShowPreview = true;

	/** Show constraint lines in editor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	bool bShowConstraintLines = true;

	/** Color for constraint lines */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	FColor ConstraintLineColor = FColor::Yellow;

	/** Color for breakable constraint lines */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	FColor BreakableLineColor = FColor::Orange;

	// ═══════════════════════════════════════════════════════════════
	// API
	// ═══════════════════════════════════════════════════════════════

	/** Spawn the group manually (if bSpawnOnBeginPlay is false) */
	UFUNCTION(BlueprintCallable, Category = "Constrained Group")
	FFlecsGroupSpawnResult SpawnGroup();

	/** Get spawn result (valid after spawn) */
	UFUNCTION(BlueprintPure, Category = "Constrained Group")
	FFlecsGroupSpawnResult GetSpawnResult() const { return SpawnResult; }

	/** Check if group was spawned */
	UFUNCTION(BlueprintPure, Category = "Constrained Group")
	bool WasSpawned() const { return SpawnResult.bSuccess; }

	// ═══════════════════════════════════════════════════════════════
	// PRESETS (Editor utilities)
	// ═══════════════════════════════════════════════════════════════

	/** Generate a chain preset */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Presets")
	void GenerateChainPreset(UStaticMesh* Mesh, int32 Count = 5, FVector Spacing = FVector(100, 0, 0), float BreakForce = 0.f);

	/** Generate a grid preset */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Presets")
	void GenerateGridPreset(UStaticMesh* Mesh, int32 Rows = 3, int32 Columns = 3, FVector Spacing = FVector(100, 100, 0), float BreakForce = 0.f);

	/** Clear all elements and constraints */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Presets")
	void ClearAll();

protected:
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> SpriteComponent;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PreviewMeshComponents;

	void UpdatePreview();
	void ClearPreviewMeshes();
#endif

private:
	UPROPERTY()
	FFlecsGroupSpawnResult SpawnResult;

	bool ValidateConfiguration() const;
};
