// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Render profile for Flecs entity spawning.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsRenderProfile.generated.h"

class UStaticMesh;
class UMaterialInterface;

/**
 * Data Asset defining render properties for entity spawning.
 *
 * Used with FEntitySpawnRequest to add ISM (Instanced Static Mesh) rendering.
 * Entities without RenderProfile won't be visible.
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsRenderProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// MESH
	// ═══════════════════════════════════════════════════════════════

	/** Static mesh to render */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh")
	TObjectPtr<UStaticMesh> Mesh;

	/** Optional material override (nullptr = use mesh default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh")
	TObjectPtr<UMaterialInterface> MaterialOverride;

	/** Mesh scale */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh")
	FVector Scale = FVector::OneVector;

	/** Rotation offset applied to mesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh")
	FRotator RotationOffset = FRotator::ZeroRotator;

	// ═══════════════════════════════════════════════════════════════
	// RENDERING
	// ═══════════════════════════════════════════════════════════════

	/** Cast shadows */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	bool bCastShadow = true;

	/** Visible in game */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	bool bVisible = true;

	/** Custom depth stencil value for outline/selection effects */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering", meta = (ClampMin = "0", ClampMax = "255"))
	int32 CustomDepthStencilValue = 0;

	/** Render in custom depth pass (for outlines) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	bool bRenderCustomDepth = false;

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	bool IsValid() const { return Mesh != nullptr; }
};
