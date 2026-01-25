// Mass Entity Spawner with editor preview visualization

#pragma once

#include "CoreMinimal.h"
#include "MassSpawner.h"
#include "MassEntityConfigAsset.h"
#include "MSPreviewSpawner.generated.h"

/**
 * A Mass Spawner that shows a preview mesh in the editor.
 * The mesh is automatically extracted from the Entity Config's visualization trait.
 * In runtime, this spawner works exactly like the regular AMassSpawner.
 *
 * Usage:
 * 1. Place this actor in the level
 * 2. Set the PreviewEntityConfig (or use PreviewMeshOverride for manual mesh)
 * 3. Configure the parent AMassSpawner settings (EntityTypes, Count, etc.)
 * 4. The preview mesh will appear in the editor at the spawner's location
 */
UCLASS()
class MASSCOMMUNITYSAMPLE_API AMSPreviewSpawner : public AMassSpawner
{
	GENERATED_BODY()

public:
	AMSPreviewSpawner();

	// Entity config to extract preview mesh from.
	// Set this to the same config as your first EntityType for automatic mesh extraction.
	UPROPERTY(EditAnywhere, Category = "Preview")
	TSoftObjectPtr<UMassEntityConfigAsset> PreviewEntityConfig;

	// Manual override for preview mesh (optional - if set, ignores PreviewEntityConfig)
	UPROPERTY(EditAnywhere, Category = "Preview")
	TSoftObjectPtr<UStaticMesh> PreviewMeshOverride;

	// Scale for the preview mesh
	UPROPERTY(EditAnywhere, Category = "Preview")
	FVector PreviewScale = FVector::OneVector;

	// Whether to show the preview mesh in editor
	UPROPERTY(EditAnywhere, Category = "Preview")
	bool bShowPreview = true;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UStaticMeshComponent> PreviewMeshComponent;

#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// Updates the preview mesh based on current settings
	void UpdatePreviewMesh();

	// Tries to extract static mesh from PreviewEntityConfig's visualization trait
	UStaticMesh* ExtractMeshFromEntityConfig() const;
#endif
};
