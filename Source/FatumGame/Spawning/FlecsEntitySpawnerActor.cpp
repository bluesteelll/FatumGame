
#include "FlecsEntitySpawnerActor.h"
#include "FlecsEntitySpawner.h"
#include "FlecsEntityDefinition.h"
#include "FlecsRenderProfile.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

AFlecsEntitySpawner::AFlecsEntitySpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	PreviewMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	PreviewMeshComponent->SetupAttachment(RootComponent);
	PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMeshComponent->SetCastShadow(false);

#if WITH_EDITORONLY_DATA
	PreviewMeshComponent->bIsEditorOnly = true;
	PreviewMeshComponent->SetHiddenInGame(true);
#endif
}

void AFlecsEntitySpawner::BeginPlay()
{
	Super::BeginPlay();

	// Hide preview mesh at runtime
	if (PreviewMeshComponent)
	{
		PreviewMeshComponent->SetVisibility(false);
	}

	// Spawn entity if configured to do so
	if (bSpawnOnBeginPlay)
	{
		SpawnEntity();

		if (bDestroyAfterSpawn && SpawnedEntityKey.IsValid())
		{
			Destroy();
		}
	}
}

FSkeletonKey AFlecsEntitySpawner::SpawnEntity()
{
	if (SpawnedEntityKey.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("AFlecsEntitySpawner: Entity already spawned! Key=%llu"),
			static_cast<uint64>(SpawnedEntityKey));
		return SpawnedEntityKey;
	}

	if (!EntityDefinition)
	{
		UE_LOG(LogTemp, Warning, TEXT("AFlecsEntitySpawner [%s]: No EntityDefinition set!"), *GetName());
		return FSkeletonKey();
	}

	// Build spawn request from EntityDefinition
	FEntitySpawnRequest Request = FEntitySpawnRequest::FromDefinition(
		EntityDefinition,
		GetActorLocation(),
		GetActorRotation()
	);

	// Apply overrides
	Request.InitialVelocity = InitialVelocity;

	if (bOverrideFocusCamera)
	{
		Request.bOverrideFocusCamera = true;
		Request.FocusCameraPositionOverride = FocusCameraPositionOverride;
		Request.FocusCameraRotationOverride = FocusCameraRotationOverride;
	}

	if (bOverrideInteractionAngle)
	{
		Request.bOverrideInteractionAngle = true;
		Request.InteractionAngleOverride = InteractionAngleOverride;
		Request.InteractionDirectionOverride = InteractionDirectionOverride;
	}

	// Scale override: modify the render profile scale if needed
	// Note: This is applied via the spawn request, the actual implementation
	// would need to handle scale override in SpawnEntity or we create a temp profile
	// For now, we'll document that scale comes from RenderProfile

	// Spawn via unified API
	SpawnedEntityKey = UFlecsEntityLibrary::SpawnEntity(this, Request);

	if (SpawnedEntityKey.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("AFlecsEntitySpawner [%s]: Spawned entity Key=%llu at %s"),
			*GetName(), static_cast<uint64>(SpawnedEntityKey), *GetActorLocation().ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("AFlecsEntitySpawner [%s]: Failed to spawn entity!"), *GetName());
	}

	return SpawnedEntityKey;
}

#if WITH_EDITOR

void AFlecsEntitySpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdatePreview();
}

void AFlecsEntitySpawner::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdatePreview();
}

void AFlecsEntitySpawner::UpdatePreview()
{
	if (!PreviewMeshComponent)
	{
		return;
	}

	// Check if we should show preview
	if (!bShowPreview || !EntityDefinition || !EntityDefinition->RenderProfile)
	{
		PreviewMeshComponent->SetVisibility(false);
		return;
	}

	UFlecsRenderProfile* RenderProfile = EntityDefinition->RenderProfile;
	if (!RenderProfile->Mesh)
	{
		PreviewMeshComponent->SetVisibility(false);
		return;
	}

	PreviewMeshComponent->SetVisibility(true);
	PreviewMeshComponent->SetStaticMesh(RenderProfile->Mesh);

	// Apply scale (override or from profile)
	FVector Scale = bOverrideScale ? ScaleOverride : RenderProfile->Scale;
	PreviewMeshComponent->SetRelativeScale3D(Scale);

	// Apply rotation offset from render profile
	PreviewMeshComponent->SetRelativeRotation(RenderProfile->RotationOffset);

	// Compensate for mesh pivot offset - center the mesh on the actor
	FBoxSphereBounds Bounds = RenderProfile->Mesh->GetBounds();
	FVector MeshPivotOffset = -Bounds.Origin * Scale;
	PreviewMeshComponent->SetRelativeLocation(MeshPivotOffset);

	// Apply material if set
	if (RenderProfile->MaterialOverride)
	{
		PreviewMeshComponent->SetMaterial(0, RenderProfile->MaterialOverride);
	}
}

#endif
