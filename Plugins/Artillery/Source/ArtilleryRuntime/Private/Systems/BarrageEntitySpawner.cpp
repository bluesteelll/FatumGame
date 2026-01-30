// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "Systems/BarrageEntitySpawner.h"
#include <atomic>
#include "BarrageDispatch.h"
#include "TransformDispatch.h"
#include "FBarragePrimitive.h"
#include "FBShapeParams.h"
#include "Skeletonize.h"
#include "ArtilleryBPLibs.h"
#include "Systems/ArtilleryDispatch.h"
#include "BasicTypes/BarrageEntityTags.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BarrageEntitySpawner)

namespace
{
	std::atomic<uint32> GEntityCounter{0};
}

// ═══════════════════════════════════════════════════════════════════════════
// FBarrageSpawnUtils - Shared spawn logic
// ═══════════════════════════════════════════════════════════════════════════

FVector FBarrageSpawnUtils::CalculateColliderSize(UStaticMesh* Mesh, FVector Scale)
{
	if (!Mesh)
	{
		return FVector(100, 100, 100);
	}

	FBoxSphereBounds Bounds = Mesh->GetBounds();
	// BoxExtent is half-size, multiply by 2 for full size, then apply scale
	return Bounds.BoxExtent * 2.0 * Scale;
}

void FBarrageSpawnUtils::ApplyBehaviorTags(UArtilleryDispatch* Artillery, FSkeletonKey Key,
										   bool bDestructible, bool bDamagesPlayer, bool bReflective)
{
	if (!Artillery || !Key.IsValid())
	{
		return;
	}

	// Must register tag container BEFORE adding tags
	if (bDestructible || bDamagesPlayer || bReflective)
	{
		Artillery->GetOrRegisterConservedTags(Key);
	}

	if (bDestructible)
	{
		Artillery->AddTagToEntity(Key, TAG_Barrage_Destructible);
	}
	if (bDamagesPlayer)
	{
		Artillery->AddTagToEntity(Key, TAG_Barrage_DamagesPlayer);
	}
	if (bReflective)
	{
		Artillery->AddTagToEntity(Key, TAG_Barrage_Reflective);
	}
}

FBarrageSpawnResult FBarrageSpawnUtils::SpawnEntity(UWorld* World, const FBarrageSpawnParams& Params)
{
	FBarrageSpawnResult Result;
	Result.EntityKey = Params.EntityKey;

	if (!World || !Params.Mesh || !Params.EntityKey.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FBarrageSpawnUtils::SpawnEntity - Invalid parameters"));
		return Result;
	}

	UBarrageDispatch* Physics = World->GetSubsystem<UBarrageDispatch>();
	UBarrageRenderManager* Renderer = UBarrageRenderManager::Get(World);
	UArtilleryDispatch* Artillery = World->GetSubsystem<UArtilleryDispatch>();

	if (!Physics)
	{
		UE_LOG(LogTemp, Error, TEXT("FBarrageSpawnUtils::SpawnEntity - No UBarrageDispatch!"));
		return Result;
	}

	// Calculate collider size
	FVector ColliderSize = Params.bAutoCollider
		? CalculateColliderSize(Params.Mesh, Params.MeshScale)
		: Params.ManualColliderSize;

	// Ensure minimum size
	ColliderSize.X = FMath::Max(ColliderSize.X, 1.0);
	ColliderSize.Y = FMath::Max(ColliderSize.Y, 1.0);
	ColliderSize.Z = FMath::Max(ColliderSize.Z, 1.0);

	// Physics body position = actor position (no pivot offset - keeps physics and render aligned)
	FVector Location = Params.WorldTransform.GetLocation();
	FQuat Rotation = Params.WorldTransform.GetRotation();

	// Create physics body at actor location
	FBBoxParams BoxParams = FBarrageBounder::GenerateBoxBounds(
		Location,
		ColliderSize.X,
		ColliderSize.Y,
		ColliderSize.Z,
		FVector3d::ZeroVector,
		FMassByCategory::MostEnemies
	);

	FBLet Body = Physics->CreatePrimitive(
		BoxParams,
		Params.EntityKey,
		static_cast<uint16>(Params.PhysicsLayer),
		Params.bIsSensor,
		false, // not force dynamic
		Params.bIsMovable
	);

	if (!FBarragePrimitive::IsNotNull(Body))
	{
		UE_LOG(LogTemp, Error, TEXT("FBarrageSpawnUtils::SpawnEntity - Failed to create physics body!"));
		return Result;
	}

	Result.BarrageKey = Body->KeyIntoBarrage;
	Result.bSuccess = true;

	// Configure physics
	if (!Params.InitialVelocity.IsNearlyZero())
	{
		FBarragePrimitive::SetVelocity(Params.InitialVelocity, Body);
	}
	FBarragePrimitive::SetGravityFactor(Params.GravityFactor, Body);

	// Apply rotation (Rotation variable already defined above for pivot offset calculation)
	if (!Rotation.IsIdentity())
	{
		FBarragePrimitive::ApplyRotation(FQuat4d(Rotation), Body);
	}

	// Apply behavior tags
	ApplyBehaviorTags(Artillery, Params.EntityKey,
					  Params.bDestructible, Params.bDamagesPlayer, Params.bReflective);

	// Add render instance - AddInstance will apply pivot offset automatically
	if (Renderer)
	{
		FTransform RenderTransform;
		RenderTransform.SetLocation(Location);
		RenderTransform.SetRotation(Rotation);
		RenderTransform.SetScale3D(Params.MeshScale);
		Result.RenderInstanceIndex = Renderer->AddInstance(Params.Mesh, Params.Material, RenderTransform, Params.EntityKey);
	}

	return Result;
}

// ═══════════════════════════════════════════════════════════════════════════
// UBarrageRenderManager
// ═══════════════════════════════════════════════════════════════════════════

UBarrageRenderManager* UBarrageRenderManager::Get(UWorld* World)
{
	return World ? World->GetSubsystem<UBarrageRenderManager>() : nullptr;
}

void UBarrageRenderManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UBarrageRenderManager::Deinitialize()
{
	MeshGroups.Empty();
	if (ManagerActor)
	{
		ManagerActor->Destroy();
		ManagerActor = nullptr;
	}
	Super::Deinitialize();
}

TStatId UBarrageRenderManager::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBarrageRenderManager, STATGROUP_Tickables);
}

UInstancedStaticMeshComponent* UBarrageRenderManager::GetOrCreateISM(UStaticMesh* InMesh, UMaterialInterface* InMaterial)
{
	if (!InMesh)
	{
		return nullptr;
	}

	// Check existing
	FMeshGroup* Group = MeshGroups.Find(InMesh);
	if (Group && Group->ISM)
	{
		return Group->ISM;
	}

	// Create manager actor if needed
	if (!ManagerActor)
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("BarrageRenderManager");
		ManagerActor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		ManagerActor->SetRootComponent(NewObject<USceneComponent>(ManagerActor, TEXT("Root")));
		ManagerActor->GetRootComponent()->RegisterComponent();
	}

	// Create ISM
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(ManagerActor);
	ISM->SetStaticMesh(InMesh);
	ISM->SetMobility(EComponentMobility::Movable);
	ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ISM->SetCastShadow(true);
	ISM->SetupAttachment(ManagerActor->GetRootComponent());
	ISM->RegisterComponent();

	if (InMaterial)
	{
		ISM->SetMaterial(0, InMaterial);
	}

	// Store
	FMeshGroup& NewGroup = MeshGroups.Add(InMesh);
	NewGroup.ISM = ISM;

	// Calculate pivot offset - this is the same for all instances of this mesh
	FBoxSphereBounds Bounds = InMesh->GetBounds();
	NewGroup.PivotOffset = -Bounds.Origin; // Will be scaled per-instance in UpdateTransforms

	UE_LOG(LogTemp, Log, TEXT("UBarrageRenderManager: Created ISM for mesh %s, PivotOffset=%s"),
		*InMesh->GetName(), *NewGroup.PivotOffset.ToString());

	return ISM;
}

int32 UBarrageRenderManager::AddInstance(UStaticMesh* InMesh, UMaterialInterface* InMaterial, const FTransform& Transform, FSkeletonKey Key)
{
	UInstancedStaticMeshComponent* ISM = GetOrCreateISM(InMesh, InMaterial);
	if (!ISM)
	{
		UE_LOG(LogTemp, Error, TEXT("ISM_DEBUG AddInstance FAILED: No ISM for mesh, Key=%llu"), static_cast<uint64>(Key));
		return INDEX_NONE;
	}

	FMeshGroup& Group = MeshGroups.FindChecked(InMesh);

	// SAFETY: Check if this key already exists to prevent orphaned instances
	if (Group.KeyToIndex.Contains(Key))
	{
		UE_LOG(LogTemp, Error, TEXT("ISM_DEBUG AddInstance DUPLICATE: Key=%llu already at Index=%d, TotalInstances=%d"),
			static_cast<uint64>(Key), Group.KeyToIndex[Key], Group.KeyToIndex.Num());
		return Group.KeyToIndex[Key];
	}

	// Apply pivot offset to center mesh on physics position
	FVector ScaledPivotOffset = Group.PivotOffset * Transform.GetScale3D();
	FVector RotatedPivotOffset = Transform.GetRotation().RotateVector(ScaledPivotOffset);

	FTransform AdjustedTransform = Transform;
	AdjustedTransform.SetLocation(Transform.GetLocation() + RotatedPivotOffset);

	// Add instance with adjusted transform
	int32 Index = ISM->AddInstance(AdjustedTransform, true);

	// Track
	Group.KeyToIndex.Add(Key, Index);
	if (Group.IndexToKey.Num() <= Index)
	{
		Group.IndexToKey.SetNum(Index + 1);
	}
	Group.IndexToKey[Index] = Key;

	bHasEntities = true;

	UE_LOG(LogTemp, Log, TEXT("ISM_DEBUG AddInstance OK: Key=%llu Index=%d Pos=%s TotalInstances=%d"),
		static_cast<uint64>(Key), Index, *Transform.GetLocation().ToString(), Group.KeyToIndex.Num());

	return Index;
}

void UBarrageRenderManager::RemoveInstance(FSkeletonKey Key)
{
	// Thread-safe: enqueue for processing on Game thread
	UE_LOG(LogTemp, Log, TEXT("ISM_DEBUG RemoveInstance ENQUEUE: Key=%llu"), static_cast<uint64>(Key));
	PendingRemovals.Enqueue(Key);
}

void UBarrageRenderManager::DoRemoveInstance(FSkeletonKey Key)
{
	UE_LOG(LogTemp, Log, TEXT("ISM_DEBUG DoRemoveInstance START: Key=%llu"), static_cast<uint64>(Key));

	for (auto& Pair : MeshGroups)
	{
		FMeshGroup& Group = Pair.Value;

		int32* IndexPtr = Group.KeyToIndex.Find(Key);
		if (IndexPtr && *IndexPtr != INDEX_NONE)
		{
			int32 Index = *IndexPtr;

			// Remove from ISM (this swaps with last!)
			if (Group.ISM)
			{
				Group.ISM->RemoveInstance(Index);
			}

			// Update tracking for swapped instance
			int32 LastIndex = Group.IndexToKey.Num() - 1;
			if (Index != LastIndex && LastIndex >= 0)
			{
				FSkeletonKey SwappedKey = Group.IndexToKey[LastIndex];
				Group.KeyToIndex[SwappedKey] = Index;
				Group.IndexToKey[Index] = SwappedKey;
			}

			Group.KeyToIndex.Remove(Key);
			Group.IndexToKey.SetNum(FMath::Max(0, Group.IndexToKey.Num() - 1));

			break;
		}
	}

	// Check if any entities left
	bHasEntities = false;
	for (auto& Pair : MeshGroups)
	{
		if (Pair.Value.KeyToIndex.Num() > 0)
		{
			bHasEntities = true;
			break;
		}
	}
}

void UBarrageRenderManager::ProcessPendingRemovals()
{
	FSkeletonKey Key;
	while (PendingRemovals.Dequeue(Key))
	{
		DoRemoveInstance(Key);
	}
}

void UBarrageRenderManager::Tick(float DeltaTime)
{
	UpdateTransforms();
}

void UBarrageRenderManager::UpdateTransforms()
{
	// Process pending removals FIRST (thread-safe dequeue from Artillery thread)
	ProcessPendingRemovals();

	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return;
	}

	for (auto& Pair : MeshGroups)
	{
		FMeshGroup& Group = Pair.Value;
		if (!Group.ISM || Group.KeyToIndex.Num() == 0)
		{
			continue;
		}

		bool bAnyUpdated = false;

		// Collect keys to remove (can't modify map while iterating)
		TArray<FSkeletonKey> KeysToRemove;

		for (auto& KeyIndex : Group.KeyToIndex)
		{
			FSkeletonKey Key = KeyIndex.Key;
			int32 Index = KeyIndex.Value;

			// Get position from Barrage
			FBLet Body = Physics->GetShapeRef(Key);
			if (FBarragePrimitive::IsNotNull(Body))
			{
				FVector3f Pos = FBarragePrimitive::GetPosition(Body);
				FQuat4f Rot = FBarragePrimitive::OptimisticGetAbsoluteRotation(Body);

				// Get current scale from the instance
				FTransform CurrentTransform;
				Group.ISM->GetInstanceTransform(Index, CurrentTransform, true);
				FVector Scale = CurrentTransform.GetScale3D();

				// Apply pivot offset scaled and rotated
				FVector ScaledPivotOffset = Group.PivotOffset * Scale;
				FVector RotatedPivotOffset = FQuat(Rot).RotateVector(ScaledPivotOffset);

				// Render position = physics position + pivot offset
				FTransform NewTransform;
				NewTransform.SetLocation(FVector(Pos) + RotatedPivotOffset);
				NewTransform.SetRotation(FQuat(Rot));
				NewTransform.SetScale3D(Scale);

				// bTeleport=true is important for physics-synced objects to prevent rendering interpolation artifacts
				Group.ISM->UpdateInstanceTransform(Index, NewTransform, true, false, true);
				bAnyUpdated = true;
			}
			else
			{
				// SAFETY NET: Body is gone (destroyed/tombstoned) but ISM instance still exists.
				// This should NOT happen if all destruction paths call RemoveInstance() before SuggestTombstone().
				// Log a warning to help identify code paths that skip ISM cleanup.
				UE_LOG(LogTemp, Warning, TEXT("ISM_ORPHAN: Key=%llu has ISM but physics body is gone! "
					"Check if destruction path calls RemoveInstance() before SuggestTombstone()."),
					static_cast<uint64>(Key));
				KeysToRemove.Add(Key);
			}
		}

		// SAFETY NET: Remove orphaned instances detected above.
		// If this code runs frequently, there's a bug in some destruction path.
		// Check the ISM_ORPHAN warnings in the log to identify which code paths
		// are calling SuggestTombstone() without RemoveInstance().
		for (const FSkeletonKey& Key : KeysToRemove)
		{
			PendingRemovals.Enqueue(Key);
		}

		if (bAnyUpdated)
		{
			Group.ISM->MarkRenderStateDirty();
		}
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// ABarrageEntitySpawner
// ═══════════════════════════════════════════════════════════════════════════

ABarrageEntitySpawner::ABarrageEntitySpawner()
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

	// Default cube
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh = CubeMesh.Object;
	}
}

void ABarrageEntitySpawner::BeginPlay()
{
	Super::BeginPlay();

	if (PreviewMeshComponent)
	{
		PreviewMeshComponent->SetVisibility(false);
	}

	EntityKey = DoSpawn();

	if (bDestroyAfterSpawn && EntityKey.IsValid())
	{
		Destroy();
	}
}

FSkeletonKey ABarrageEntitySpawner::DoSpawn()
{
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("BarrageEntitySpawner [%s]: No mesh!"), *GetName());
		return FSkeletonKey();
	}

	// Generate key
	const uint32 Id = ++GEntityCounter;
	FSkeletonKey Key = FSkeletonKey(FORGE_SKELETON_KEY(Id, SKELLY::SFIX_BAR_PRIM));

	// Fill spawn params
	FBarrageSpawnParams Params;
	Params.Mesh = Mesh;
	Params.Material = Material;
	Params.WorldTransform = GetActorTransform();
	Params.EntityKey = Key;
	Params.MeshScale = MeshScale;
	Params.PhysicsLayer = PhysicsLayer;
	Params.bAutoCollider = bAutoCollider;
	Params.ManualColliderSize = ColliderSize;
	Params.bIsMovable = bIsMovable;
	Params.bIsSensor = bIsSensor;
	Params.InitialVelocity = InitialVelocity;
	Params.GravityFactor = GravityFactor;
	Params.bDestructible = bDestructible;
	Params.bDamagesPlayer = bDamagesPlayer;
	Params.bReflective = bReflective;

	// Use shared spawn logic
	FBarrageSpawnResult Result = FBarrageSpawnUtils::SpawnEntity(GetWorld(), Params);

	if (Result.bSuccess)
	{
		InstanceIndex = Result.RenderInstanceIndex;
		UE_LOG(LogTemp, Log, TEXT("BarrageEntitySpawner: Spawned %s Key=%llu at %s"),
			*GetName(), static_cast<uint64>(Key), *GetActorLocation().ToString());
	}

	return Result.bSuccess ? Key : FSkeletonKey();
}

FSkeletonKey ABarrageEntitySpawner::SpawnEntity(
	UObject* WorldContextObject,
	UStaticMesh* InMesh,
	FTransform Transform,
	FVector InMeshScale,
	EPhysicsLayer InPhysicsLayer,
	bool bInIsMovable,
	FVector InVelocity,
	float InGravity)
{
	if (!WorldContextObject || !InMesh)
	{
		return FSkeletonKey();
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return FSkeletonKey();
	}

	// Generate key
	const uint32 Id = ++GEntityCounter;
	FSkeletonKey Key = FSkeletonKey(FORGE_SKELETON_KEY(Id, SKELLY::SFIX_BAR_PRIM));

	// Fill spawn params
	FBarrageSpawnParams Params;
	Params.Mesh = InMesh;
	Params.WorldTransform = Transform;
	Params.EntityKey = Key;
	Params.MeshScale = InMeshScale;
	Params.PhysicsLayer = InPhysicsLayer;
	Params.bAutoCollider = true;
	Params.bIsMovable = bInIsMovable;
	Params.InitialVelocity = InVelocity;
	Params.GravityFactor = InGravity;

	// Use shared spawn logic
	FBarrageSpawnResult Result = FBarrageSpawnUtils::SpawnEntity(World, Params);

	return Result.bSuccess ? Key : FSkeletonKey();
}

void ABarrageEntitySpawner::DestroyEntity(FSkeletonKey InEntityKey)
{
	if (!InEntityKey.IsValid())
	{
		return;
	}

	// Remove from render manager
	if (UBarrageDispatch::SelfPtr && UBarrageDispatch::SelfPtr->GetWorld())
	{
		if (UBarrageRenderManager* Renderer = UBarrageRenderManager::Get(UBarrageDispatch::SelfPtr->GetWorld()))
		{
			Renderer->RemoveInstance(InEntityKey);
		}
	}

	// Get the physics primitive and destroy it
	if (UBarrageDispatch::SelfPtr)
	{
		FBLet Prim = UBarrageDispatch::SelfPtr->GetShapeRef(InEntityKey);
		if (FBarragePrimitive::IsNotNull(Prim))
		{
			// Wake up nearby sleeping bodies BEFORE destroying (per Jolt recommendation)
			UBarrageDispatch::SelfPtr->ActivateBodiesAroundBody(Prim->KeyIntoBarrage, 0.1f);

			// Mark physics body for deferred destruction via tombstone.
			// DO NOT call FinalizeReleasePrimitive() - it is called automatically
			// by ~FBarragePrimitive() when tombstone expires and ref count reaches 0.
			// Calling both causes double-free crash!
			UBarrageDispatch::SelfPtr->SuggestTombstone(Prim);
		}
	}
}

#if WITH_EDITOR

void ABarrageEntitySpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdatePreview();
}

void ABarrageEntitySpawner::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdatePreview();
}

void ABarrageEntitySpawner::UpdatePreview()
{
	if (!PreviewMeshComponent)
	{
		return;
	}

	if (!bShowPreview || !Mesh)
	{
		PreviewMeshComponent->SetVisibility(false);
		return;
	}

	PreviewMeshComponent->SetVisibility(true);
	PreviewMeshComponent->SetStaticMesh(Mesh);
	PreviewMeshComponent->SetRelativeScale3D(MeshScale);

	// Compensate for mesh pivot offset - center the mesh on the actor
	// Mesh bounds origin is relative to mesh pivot, so we need to offset by -Origin to center it
	FBoxSphereBounds Bounds = Mesh->GetBounds();
	FVector MeshPivotOffset = -Bounds.Origin * MeshScale; // Offset to center the mesh
	PreviewMeshComponent->SetRelativeLocation(MeshPivotOffset);

	if (Material)
	{
		PreviewMeshComponent->SetMaterial(0, Material);
	}
}

#endif
