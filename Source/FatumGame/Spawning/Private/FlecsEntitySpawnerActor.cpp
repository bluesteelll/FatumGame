
#include "FlecsEntitySpawnerActor.h"
#include "FlecsEntitySpawner.h"
#include "FlecsEntityDefinition.h"
#include "FlecsRenderProfile.h"
#include "FlecsSpawnerComponents.h"     // FSpawnerProvenance (Phase 5)
#include "FlecsArtillerySubsystem.h"    // EnqueueCommand + GetEntityForBarrageKey
#include "FlecsBarrageComponents.h"     // FBarrageBody for sanity
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "flecs.h"

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

	// Phase 5 save-system dedup (v2 §5.3 + v3 §A): UFlecsSaveSubsystem::DeferredLoadTick
	// scans the loaded snapshot for FSpawnerProvenance tuples and marks matching spawners
	// with bSavedEntityOverridesMe BEFORE their BeginPlay runs. When set, skip the spawn
	// and self-destroy if configured to — the loaded entity is the sole authority.
	if (bSavedEntityOverridesMe)
	{
		UE_LOG(LogTemp, Verbose, TEXT("AFlecsEntitySpawner [%s]: saved entity overrides — skipping BeginPlay spawn"),
			*GetName());
		if (bDestroyAfterSpawn)
		{
			Destroy();
		}
		return;
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
	if (ItemCount > 1)
	{
		Request.ItemCount = ItemCount;
	}

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

		// Phase 5 — stamp FSpawnerProvenance on the new entity so the save walker can
		// later identify which spawner produced it. SpawnEntity enqueues entity creation
		// onto the sim thread; we enqueue this stamp command immediately AFTER, which
		// guarantees FIFO order (MPSC queue drains in submission order on the sim thread).
		//
		// Per v3 §D — never capture flecs::world& by reference into a sim-thread lambda;
		// the outer stack frame is gone by lambda execution. Resolve the world via the
		// captured FlecsSubsystem at lambda execution time.
		UWorld* World = GetWorld();
		UFlecsArtillerySubsystem* FlecsSubsystem = World ? World->GetSubsystem<UFlecsArtillerySubsystem>() : nullptr;
		if (FlecsSubsystem)
		{
			// Capture level path with PIE prefix stripped so save-in-PIE / load-in-PIE
			// with a different instance id still match (per v2 §5.3).
			const FString RawLevelPath = GetLevel() ? GetLevel()->GetPathName() : FString();
			const FString LevelPath = UWorld::RemovePIEPrefix(RawLevelPath);
			const FName LevelPathName(*LevelPath);
			const FName ActorName = GetFName();
			const FSkeletonKey CapturedKey = SpawnedEntityKey;

			FlecsSubsystem->EnqueueCommand([FlecsSubsystem, CapturedKey, LevelPathName, ActorName]()
			{
				flecs::entity E = FlecsSubsystem->GetEntityForBarrageKey(CapturedKey);
				if (!E.is_valid() || !E.is_alive())
				{
					// SpawnEntity may have failed for non-physics paths; that's fine —
					// non-world entities (containers, etc.) are out of scope for the spawner
					// dedup pass since they have no level placement.
					UE_LOG(LogTemp, Verbose,
						TEXT("AFlecsEntitySpawner provenance: entity for Key=%llu not alive — skipping provenance stamp"),
						static_cast<uint64>(CapturedKey));
					return;
				}
				FSpawnerProvenance P;
				P.SpawnerLevelPath = LevelPathName;
				P.SpawnerActorName = ActorName;
				E.set<FSpawnerProvenance>(P);
			});
		}
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
