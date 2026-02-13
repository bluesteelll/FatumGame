
#include "FlecsDestructibleSpawner.h"
#include "FlecsEntitySpawner.h"
#include "FlecsEntityDefinition.h"
#include "FlecsDestructibleProfile.h"
#include "FlecsRenderProfile.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

AFlecsDestructibleSpawner::AFlecsDestructibleSpawner()
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

void AFlecsDestructibleSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (PreviewMeshComponent)
	{
		PreviewMeshComponent->SetVisibility(false);
	}

	if (bSpawnOnBeginPlay)
	{
		SpawnDestructible();

		if (bDestroyAfterSpawn && SpawnedEntityKey.IsValid())
		{
			Destroy();
		}
	}
}

FSkeletonKey AFlecsDestructibleSpawner::SpawnDestructible()
{
	if (SpawnedEntityKey.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("AFlecsDestructibleSpawner: Already spawned!"));
		return SpawnedEntityKey;
	}

	if (!EntityDefinition)
	{
		UE_LOG(LogTemp, Warning, TEXT("AFlecsDestructibleSpawner [%s]: No EntityDefinition set!"), *GetName());
		return FSkeletonKey();
	}

	if (!EntityDefinition->DestructibleProfile)
	{
		UE_LOG(LogTemp, Warning, TEXT("AFlecsDestructibleSpawner [%s]: EntityDefinition has no DestructibleProfile!"), *GetName());
		return FSkeletonKey();
	}

	if (!EntityDefinition->DestructibleProfile->IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("AFlecsDestructibleSpawner [%s]: DestructibleProfile has no Geometry!"), *GetName());
		return FSkeletonKey();
	}

	// Spawn via unified API — all profile handling (physics, render, health, etc.) is done there.
	// DestructibleProfile → FDestructibleStatic component is also handled by SpawnEntity.
	FEntitySpawnRequest Request = FEntitySpawnRequest::FromDefinition(
		EntityDefinition,
		GetActorLocation(),
		GetActorRotation()
	);

	SpawnedEntityKey = UFlecsEntityLibrary::SpawnEntity(this, Request);

	if (SpawnedEntityKey.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("AFlecsDestructibleSpawner [%s]: Spawned destructible Key=%llu at %s"),
			*GetName(), static_cast<uint64>(SpawnedEntityKey), *GetActorLocation().ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("AFlecsDestructibleSpawner [%s]: Failed to spawn!"), *GetName());
	}

	return SpawnedEntityKey;
}

#if WITH_EDITOR

void AFlecsDestructibleSpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdatePreview();
}

void AFlecsDestructibleSpawner::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdatePreview();
}

void AFlecsDestructibleSpawner::UpdatePreview()
{
	if (!PreviewMeshComponent)
	{
		return;
	}

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
	PreviewMeshComponent->SetRelativeScale3D(RenderProfile->Scale);
	PreviewMeshComponent->SetRelativeRotation(RenderProfile->RotationOffset);

	// Center mesh on actor
	FBoxSphereBounds Bounds = RenderProfile->Mesh->GetBounds();
	FVector MeshPivotOffset = -Bounds.Origin * RenderProfile->Scale;
	PreviewMeshComponent->SetRelativeLocation(MeshPivotOffset);

	if (RenderProfile->MaterialOverride)
	{
		PreviewMeshComponent->SetMaterial(0, RenderProfile->MaterialOverride);
	}
}

#endif
