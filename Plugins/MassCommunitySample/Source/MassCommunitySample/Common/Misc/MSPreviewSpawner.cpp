// Mass Entity Spawner with editor preview visualization

#include "MSPreviewSpawner.h"
#include "MassEntityConfigAsset.h"
#include "Representation/Traits/MSNiagaraRepresentationTraits.h"
#include "Components/StaticMeshComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MSPreviewSpawner)

AMSPreviewSpawner::AMSPreviewSpawner()
{
	PreviewMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	PreviewMeshComponent->SetupAttachment(RootComponent);

#if WITH_EDITORONLY_DATA
	PreviewMeshComponent->bIsEditorOnly = true;
	PreviewMeshComponent->SetHiddenInGame(true);
#endif
}

#if WITH_EDITOR

void AMSPreviewSpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdatePreviewMesh();
}

void AMSPreviewSpawner::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdatePreviewMesh();
}

void AMSPreviewSpawner::UpdatePreviewMesh()
{
	if (!PreviewMeshComponent)
	{
		return;
	}

	if (!bShowPreview)
	{
		PreviewMeshComponent->SetVisibility(false);
		return;
	}

	PreviewMeshComponent->SetVisibility(true);
	PreviewMeshComponent->SetRelativeScale3D(PreviewScale);

	// First check if there's a manual override
	if (!PreviewMeshOverride.IsNull())
	{
		UStaticMesh* OverrideMesh = PreviewMeshOverride.LoadSynchronous();
		if (OverrideMesh)
		{
			PreviewMeshComponent->SetStaticMesh(OverrideMesh);
			return;
		}
	}

	// Otherwise try to extract from Entity Config
	UStaticMesh* ExtractedMesh = ExtractMeshFromEntityConfig();
	if (ExtractedMesh)
	{
		PreviewMeshComponent->SetStaticMesh(ExtractedMesh);
	}
	else
	{
		PreviewMeshComponent->SetStaticMesh(nullptr);
	}
}

UStaticMesh* AMSPreviewSpawner::ExtractMeshFromEntityConfig() const
{
	// Load the preview entity config
	UMassEntityConfigAsset* ConfigAsset = PreviewEntityConfig.LoadSynchronous();
	if (!ConfigAsset)
	{
		return nullptr;
	}

	const FMassEntityConfig& Config = ConfigAsset->GetConfig();

	// Iterate through traits to find visualization-related ones
	for (const UMassEntityTraitBase* Trait : Config.GetTraits())
	{
		if (!Trait)
		{
			continue;
		}

		// Check for our custom Niagara representation trait first (it has direct mesh reference)
		if (const UMSNiagaraRepresentationTrait* NiagaraTrait = Cast<UMSNiagaraRepresentationTrait>(Trait))
		{
			if (!NiagaraTrait->StaticMesh.IsNull())
			{
				return NiagaraTrait->StaticMesh.LoadSynchronous();
			}
		}

		// For standard UMassVisualizationTrait, the mesh structure is complex
		// (nested in StaticMeshInstanceDesc.ISMComponentDescriptors)
		// Use PreviewMeshOverride if you need to preview entities with UMassVisualizationTrait
	}

	return nullptr;
}

#endif // WITH_EDITOR
