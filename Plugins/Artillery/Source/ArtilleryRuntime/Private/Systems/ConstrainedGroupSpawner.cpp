// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "Systems/ConstrainedGroupSpawner.h"
#include <atomic>
#include "BarrageDispatch.h"
#include "BarrageConstraintSystem.h"
#include "FBarragePrimitive.h"
#include "Skeletonize.h"
#include "Systems/BarrageEntitySpawner.h" // For FBarrageSpawnUtils
#include "Components/StaticMeshComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "DrawDebugHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ConstrainedGroupSpawner)

namespace
{
	std::atomic<uint32> GConstrainedGroupCounter{1000000}; // Start high to avoid collision with regular entities
}

// ═══════════════════════════════════════════════════════════════════════════
// AConstrainedGroupSpawner
// ═══════════════════════════════════════════════════════════════════════════

AConstrainedGroupSpawner::AConstrainedGroupSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = RootSceneComponent;

#if WITH_EDITORONLY_DATA
	// Create line batch component for connection visualization
	ConnectionLinesComponent = CreateDefaultSubobject<ULineBatchComponent>(TEXT("ConnectionLines"));
	ConnectionLinesComponent->SetupAttachment(RootComponent);
	ConnectionLinesComponent->bIsEditorOnly = true;
#endif
}

void AConstrainedGroupSpawner::BeginPlay()
{
	Super::BeginPlay();

	// Destroy preview meshes - they are editor-only and should not exist at runtime
	for (UStaticMeshComponent* Preview : PreviewMeshes)
	{
		if (Preview && IsValid(Preview))
		{
			Preview->UnregisterComponent();
			Preview->DestroyComponent();
		}
	}
	PreviewMeshes.Empty();

#if WITH_EDITORONLY_DATA
	// Destroy editor-only visualization components
	for (USphereComponent* Sphere : AnchorPointSpheres)
	{
		if (Sphere && IsValid(Sphere))
		{
			Sphere->UnregisterComponent();
			Sphere->DestroyComponent();
		}
	}
	AnchorPointSpheres.Empty();

	if (ConnectionLinesComponent && IsValid(ConnectionLinesComponent))
	{
		ConnectionLinesComponent->Flush();
	}
#endif

	// Spawn physics bodies
	SpawnBodies();

	// Create constraints between them
	CreateConstraints();

	bSpawned = true;

	// Enable tick if auto-processing
	if (bAutoProcessBreaking)
	{
		SetActorTickEnabled(true);
	}

	if (bDestroyAfterSpawn)
	{
		// Don't actually destroy - just disable tick and hide
		// The physics bodies and constraints live on
		SetActorHiddenInGame(true);
		SetActorTickEnabled(false);
	}

	UE_LOG(LogTemp, Log, TEXT("ConstrainedGroupSpawner [%s]: Spawned %d parts with %d connections"),
		*GetName(), Parts.Num(), Connections.Num());
}

void AConstrainedGroupSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyAllParts();
	Super::EndPlay(EndPlayReason);
}

void AConstrainedGroupSpawner::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bAutoProcessBreaking && bSpawned)
	{
		ProcessBreaking();
	}

	// Runtime debug visualization (when playing)
	if (bShowConnectionLines && bSpawned)
	{
		UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
		if (Physics)
		{
			for (const FConstrainedConnection& Conn : Connections)
			{
				if (!Parts.IsValidIndex(Conn.PartIndexA) || !Parts.IsValidIndex(Conn.PartIndexB))
				{
					continue;
				}

				const FConstrainedBodyPart& PartA = Parts[Conn.PartIndexA];
				const FConstrainedBodyPart& PartB = Parts[Conn.PartIndexB];

				FBLet BodyA = Physics->GetShapeRef(PartA.BodyKey);
				FBLet BodyB = Physics->GetShapeRef(PartB.BodyKey);

				if (FBarragePrimitive::IsNotNull(BodyA) && FBarragePrimitive::IsNotNull(BodyB))
				{
					FVector PosA = FVector(FBarragePrimitive::GetPosition(BodyA));
					FVector PosB = FVector(FBarragePrimitive::GetPosition(BodyB));

					// Offset anchor points relative to body positions
					FQuat4f RotA = FBarragePrimitive::OptimisticGetAbsoluteRotation(BodyA);
					FQuat4f RotB = FBarragePrimitive::OptimisticGetAbsoluteRotation(BodyB);

					FVector AnchorA = PosA + FQuat(RotA).RotateVector(Conn.LocalAnchorA);
					FVector AnchorB = PosB + FQuat(RotB).RotateVector(Conn.LocalAnchorB);

#if WITH_EDITOR
					FColor LineColor = Conn.bIsIntact ? GetColorForConstraintType(Conn.ConstraintType) : BrokenConnectionColor;
#else
					FColor LineColor = Conn.bIsIntact ? FColor::Green : FColor::Red;
#endif

					DrawDebugLine(GetWorld(), AnchorA, AnchorB, LineColor, false, -1.0f, 0, ConnectionLineThickness);

					// Draw anchor points
					if (bShowAnchorPoints)
					{
						DrawDebugSphere(GetWorld(), AnchorA, AnchorPointRadius, 8, AnchorPointColor, false, -1.0f);
						DrawDebugSphere(GetWorld(), AnchorB, AnchorPointRadius, 8, AnchorPointColor, false, -1.0f);
					}
				}
			}
		}
	}
}

void AConstrainedGroupSpawner::SpawnBodies()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("ConstrainedGroupSpawner: No World!"));
		return;
	}

	FVector SpawnerLocation = GetActorLocation();
	FRotator SpawnerRotation = GetActorRotation();

	for (int32 i = 0; i < Parts.Num(); ++i)
	{
		FConstrainedBodyPart& Part = Parts[i];

		if (!Part.Mesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("ConstrainedGroupSpawner [%s]: Part %d has no mesh!"), *GetName(), i);
			continue;
		}

		// Generate unique key
		const uint32 Id = ++GConstrainedGroupCounter;
		Part.BodyKey = FSkeletonKey(FORGE_SKELETON_KEY(Id, SKELLY::SFIX_BAR_PRIM));

		// Calculate world transform
		FVector WorldPos = SpawnerLocation + SpawnerRotation.RotateVector(Part.LocalOffset);
		FRotator WorldRot = SpawnerRotation + Part.LocalRotation;

		FTransform WorldTransform;
		WorldTransform.SetLocation(WorldPos);
		WorldTransform.SetRotation(WorldRot.Quaternion());

		// Fill spawn params - SAME logic as BarrageEntitySpawner
		FBarrageSpawnParams Params;
		Params.Mesh = Part.Mesh;
		Params.Material = Part.Material;
		Params.WorldTransform = WorldTransform;
		Params.EntityKey = Part.BodyKey;
		Params.MeshScale = Part.Scale;
		Params.PhysicsLayer = PhysicsLayer;
		Params.bAutoCollider = Part.bAutoCollider;
		Params.ManualColliderSize = Part.ColliderSize;
		Params.bIsMovable = Part.bIsMovable;
		Params.bIsSensor = false;
		Params.InitialVelocity = Part.InitialVelocity;
		// Use part gravity if non-default, otherwise use spawner default
		Params.GravityFactor = (Part.GravityFactor != 1.0f) ? Part.GravityFactor : DefaultGravityFactor;
		// Use part setting OR default setting
		Params.bDestructible = Part.bDestructible || bDefaultDestructible;
		Params.bDamagesPlayer = Part.bDamagesPlayer || bDefaultDamagesPlayer;
		Params.bReflective = Part.bReflective || bDefaultReflective;

		// Use shared spawn logic - SAME as BarrageEntitySpawner
		FBarrageSpawnResult Result = FBarrageSpawnUtils::SpawnEntity(World, Params);

		if (!Result.bSuccess)
		{
			UE_LOG(LogTemp, Error, TEXT("ConstrainedGroupSpawner: Failed to create physics body for part %d!"), i);
			continue;
		}

		// Store results for constraint creation
		Part.BarrageKey = Result.BarrageKey;
		Part.InstanceIndex = Result.RenderInstanceIndex;

		UE_LOG(LogTemp, Verbose, TEXT("ConstrainedGroupSpawner: Created part %d '%s' at %s"),
			i, *Part.PartName.ToString(), *WorldPos.ToString());
	}
}

void AConstrainedGroupSpawner::CreateConstraints()
{
	UWorld* World = GetWorld();
	UBarrageDispatch* Physics = World->GetSubsystem<UBarrageDispatch>();

	if (!Physics)
	{
		return;
	}

	FBarrageConstraintSystem* ConstraintSys = Physics->GetConstraintSystem();
	if (!ConstraintSys)
	{
		UE_LOG(LogTemp, Error, TEXT("ConstrainedGroupSpawner: No constraint system!"));
		return;
	}

	FVector SpawnerLocation = GetActorLocation();
	FRotator SpawnerRotation = GetActorRotation();

	for (int32 i = 0; i < Connections.Num(); ++i)
	{
		FConstrainedConnection& Conn = Connections[i];

		// Validate indices
		if (!Parts.IsValidIndex(Conn.PartIndexA) || !Parts.IsValidIndex(Conn.PartIndexB))
		{
			UE_LOG(LogTemp, Warning, TEXT("ConstrainedGroupSpawner: Connection %d has invalid part indices!"), i);
			continue;
		}

		const FConstrainedBodyPart& PartA = Parts[Conn.PartIndexA];
		const FConstrainedBodyPart& PartB = Parts[Conn.PartIndexB];

		if (!PartA.BarrageKey.KeyIntoBarrage || !PartB.BarrageKey.KeyIntoBarrage)
		{
			UE_LOG(LogTemp, Warning, TEXT("ConstrainedGroupSpawner: Connection %d references parts without physics bodies!"), i);
			continue;
		}

		// Calculate world anchor points
		FVector WorldAnchorA = SpawnerLocation + SpawnerRotation.RotateVector(PartA.LocalOffset + Conn.LocalAnchorA);
		FVector WorldAnchorB = SpawnerLocation + SpawnerRotation.RotateVector(PartB.LocalOffset + Conn.LocalAnchorB);

		// Create constraint based on type
		switch (Conn.ConstraintType)
		{
		case EBConstraintType::Fixed:
			{
				FBFixedConstraintParams Params;
				Params.Body1 = PartA.BarrageKey;
				Params.Body2 = PartB.BarrageKey;
				Params.Space = EBConstraintSpace::WorldSpace;
				Params.bAutoDetectAnchor = false;
				Params.AnchorPoint1 = WorldAnchorA;
				Params.AnchorPoint2 = WorldAnchorB;
				Params.BreakForce = Conn.BreakForce;
				Params.BreakTorque = Conn.BreakTorque;
				Conn.ConstraintKey = ConstraintSys->CreateFixed(Params);
			}
			break;

		case EBConstraintType::Point:
			{
				FBPointConstraintParams Params;
				Params.Body1 = PartA.BarrageKey;
				Params.Body2 = PartB.BarrageKey;
				Params.Space = EBConstraintSpace::WorldSpace;
				Params.AnchorPoint1 = WorldAnchorA;
				Params.AnchorPoint2 = WorldAnchorB;
				Params.BreakForce = Conn.BreakForce;
				Params.BreakTorque = Conn.BreakTorque;
				Conn.ConstraintKey = ConstraintSys->CreatePoint(Params);
			}
			break;

		case EBConstraintType::Hinge:
			{
				FBHingeConstraintParams Params;
				Params.Body1 = PartA.BarrageKey;
				Params.Body2 = PartB.BarrageKey;
				Params.Space = EBConstraintSpace::WorldSpace;
				Params.AnchorPoint1 = WorldAnchorA;
				Params.AnchorPoint2 = WorldAnchorB;
				Params.HingeAxis = SpawnerRotation.RotateVector(Conn.HingeAxis);
				Params.bHasLimits = Conn.bHingeLimits;
				Params.MinAngle = FMath::DegreesToRadians(Conn.MinAngle);
				Params.MaxAngle = FMath::DegreesToRadians(Conn.MaxAngle);
				Params.BreakForce = Conn.BreakForce;
				Params.BreakTorque = Conn.BreakTorque;
				Conn.ConstraintKey = ConstraintSys->CreateHinge(Params);
			}
			break;

		case EBConstraintType::Distance:
			{
				FBDistanceConstraintParams Params;
				Params.Body1 = PartA.BarrageKey;
				Params.Body2 = PartB.BarrageKey;
				Params.Space = EBConstraintSpace::WorldSpace;
				Params.AnchorPoint1 = WorldAnchorA;
				Params.AnchorPoint2 = WorldAnchorB;
				Params.MinDistance = Conn.MinDistance;
				Params.MaxDistance = Conn.MaxDistance;
				Params.SpringFrequency = Conn.SpringFrequency;
				Params.BreakForce = Conn.BreakForce;
				Params.BreakTorque = Conn.BreakTorque;
				Conn.ConstraintKey = ConstraintSys->CreateDistance(Params);
			}
			break;

		default:
			UE_LOG(LogTemp, Warning, TEXT("ConstrainedGroupSpawner: Unsupported constraint type for connection %d"), i);
			continue;
		}

		Conn.bIsIntact = Conn.ConstraintKey.IsValid();

		if (Conn.bIsIntact)
		{
			UE_LOG(LogTemp, Verbose, TEXT("ConstrainedGroupSpawner: Created %s constraint between part %d and %d"),
				*UEnum::GetValueAsString(Conn.ConstraintType), Conn.PartIndexA, Conn.PartIndexB);
		}
	}
}

void AConstrainedGroupSpawner::ProcessBreaking()
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return;
	}

	FBarrageConstraintSystem* ConstraintSys = Physics->GetConstraintSystem();
	if (!ConstraintSys)
	{
		return;
	}

	for (int32 i = 0; i < Connections.Num(); ++i)
	{
		FConstrainedConnection& Conn = Connections[i];

		if (Conn.bIsIntact && Conn.ConstraintKey.IsValid())
		{
			if (ConstraintSys->ShouldBreak(Conn.ConstraintKey))
			{
				// Remove the constraint
				ConstraintSys->Remove(Conn.ConstraintKey);
				Conn.bIsIntact = false;
				Conn.ConstraintKey = FBarrageConstraintKey();

				// Fire event
				OnConnectionBroken.Broadcast(i, Conn.PartIndexA, Conn.PartIndexB);

				UE_LOG(LogTemp, Log, TEXT("ConstrainedGroupSpawner [%s]: Connection %d broke! (Part %d <-> Part %d)"),
					*GetName(), i, Conn.PartIndexA, Conn.PartIndexB);

				// Check if we should destroy loose parts
				if (bDestroyLooseParts)
				{
					if (GetIntactConnectionCount(Conn.PartIndexA) == 0)
					{
						CleanupPart(Conn.PartIndexA);
					}
					if (GetIntactConnectionCount(Conn.PartIndexB) == 0)
					{
						CleanupPart(Conn.PartIndexB);
					}
				}
			}
		}
	}
}

void AConstrainedGroupSpawner::CleanupPart(int32 PartIndex)
{
	if (!Parts.IsValidIndex(PartIndex))
	{
		return;
	}

	FConstrainedBodyPart& Part = Parts[PartIndex];
	if (!Part.BodyKey.IsValid())
	{
		return;
	}

	// Remove from renderer
	if (UBarrageDispatch::SelfPtr && UBarrageDispatch::SelfPtr->GetWorld())
	{
		if (UBarrageRenderManager* Renderer = UBarrageRenderManager::Get(UBarrageDispatch::SelfPtr->GetWorld()))
		{
			Renderer->RemoveInstance(Part.BodyKey);
		}
	}

	// Destroy physics body
	if (UBarrageDispatch::SelfPtr)
	{
		FBLet Prim = UBarrageDispatch::SelfPtr->GetShapeRef(Part.BodyKey);
		if (FBarragePrimitive::IsNotNull(Prim))
		{
			UBarrageDispatch::SelfPtr->ActivateBodiesAroundBody(Prim->KeyIntoBarrage, 0.1f);
			UBarrageDispatch::SelfPtr->SuggestTombstone(Prim);
			UBarrageDispatch::SelfPtr->FinalizeReleasePrimitive(Prim->KeyIntoBarrage);
		}
	}

	Part.BodyKey = FSkeletonKey();
	Part.BarrageKey = FBarrageKey();
	Part.InstanceIndex = INDEX_NONE;
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════

FSkeletonKey AConstrainedGroupSpawner::GetPartKey(int32 PartIndex) const
{
	if (Parts.IsValidIndex(PartIndex))
	{
		return Parts[PartIndex].BodyKey;
	}
	return FSkeletonKey();
}

TArray<FSkeletonKey> AConstrainedGroupSpawner::GetAllPartKeys() const
{
	TArray<FSkeletonKey> Keys;
	for (const FConstrainedBodyPart& Part : Parts)
	{
		if (Part.BodyKey.IsValid())
		{
			Keys.Add(Part.BodyKey);
		}
	}
	return Keys;
}

bool AConstrainedGroupSpawner::BreakConnection(int32 ConnectionIndex)
{
	if (!Connections.IsValidIndex(ConnectionIndex))
	{
		return false;
	}

	FConstrainedConnection& Conn = Connections[ConnectionIndex];
	if (!Conn.bIsIntact)
	{
		return false;
	}

	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (Physics)
	{
		if (FBarrageConstraintSystem* ConstraintSys = Physics->GetConstraintSystem())
		{
			ConstraintSys->Remove(Conn.ConstraintKey);
		}
	}

	Conn.bIsIntact = false;
	Conn.ConstraintKey = FBarrageConstraintKey();
	OnConnectionBroken.Broadcast(ConnectionIndex, Conn.PartIndexA, Conn.PartIndexB);

	return true;
}

int32 AConstrainedGroupSpawner::BreakAllConnectionsToPart(int32 PartIndex)
{
	int32 BrokenCount = 0;
	for (int32 i = 0; i < Connections.Num(); ++i)
	{
		FConstrainedConnection& Conn = Connections[i];
		if (Conn.bIsIntact && (Conn.PartIndexA == PartIndex || Conn.PartIndexB == PartIndex))
		{
			if (BreakConnection(i))
			{
				BrokenCount++;
			}
		}
	}
	return BrokenCount;
}

bool AConstrainedGroupSpawner::IsConnectionIntact(int32 ConnectionIndex) const
{
	if (Connections.IsValidIndex(ConnectionIndex))
	{
		return Connections[ConnectionIndex].bIsIntact;
	}
	return false;
}

int32 AConstrainedGroupSpawner::GetIntactConnectionCount(int32 PartIndex) const
{
	int32 Count = 0;
	for (const FConstrainedConnection& Conn : Connections)
	{
		if (Conn.bIsIntact && (Conn.PartIndexA == PartIndex || Conn.PartIndexB == PartIndex))
		{
			Count++;
		}
	}
	return Count;
}

void AConstrainedGroupSpawner::ApplyImpulseToPart(int32 PartIndex, FVector Impulse)
{
	if (!Parts.IsValidIndex(PartIndex))
	{
		return;
	}

	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return;
	}

	FBLet Body = Physics->GetShapeRef(Parts[PartIndex].BodyKey);
	if (FBarragePrimitive::IsNotNull(Body))
	{
		// Impulse approximation: Apply as a large force for one tick
		// Impulse = Force * dt, so Force = Impulse / dt ~= Impulse * TickRate
		// Artillery runs at ~120Hz, so multiply by 120 to approximate impulse
		constexpr double TickRateApprox = 120.0;
		FBarragePrimitive::ApplyForce(FVector3d(Impulse) * TickRateApprox, Body);
	}
}

void AConstrainedGroupSpawner::DestroyAllParts()
{
	// First remove all constraints
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (Physics)
	{
		if (FBarrageConstraintSystem* ConstraintSys = Physics->GetConstraintSystem())
		{
			for (FConstrainedConnection& Conn : Connections)
			{
				if (Conn.bIsIntact && Conn.ConstraintKey.IsValid())
				{
					ConstraintSys->Remove(Conn.ConstraintKey);
					Conn.bIsIntact = false;
					Conn.ConstraintKey = FBarrageConstraintKey();
				}
			}
		}
	}

	// Then destroy all parts
	for (int32 i = 0; i < Parts.Num(); ++i)
	{
		CleanupPart(i);
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// Editor
// ═══════════════════════════════════════════════════════════════════════════

#if WITH_EDITOR

void AConstrainedGroupSpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdatePreview();
}

void AConstrainedGroupSpawner::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdatePreview();
}

void AConstrainedGroupSpawner::UpdatePreview()
{
	// Clean up old previews - must unregister before destroy for proper editor cleanup
	for (UStaticMeshComponent* Preview : PreviewMeshes)
	{
		if (Preview && IsValid(Preview))
		{
			Preview->UnregisterComponent();
			Preview->DestroyComponent();
		}
	}
	PreviewMeshes.Empty();

	if (bShowPreview)
	{
		// Create preview for each part
		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			const FConstrainedBodyPart& Part = Parts[i];

			if (!Part.Mesh)
			{
				PreviewMeshes.Add(nullptr);
				continue;
			}

			UStaticMeshComponent* Preview = NewObject<UStaticMeshComponent>(this);
			Preview->SetStaticMesh(Part.Mesh);
			Preview->SetRelativeLocation(Part.LocalOffset);
			Preview->SetRelativeRotation(Part.LocalRotation);
			Preview->SetRelativeScale3D(Part.Scale);
			Preview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Preview->SetCastShadow(false);
			Preview->bIsEditorOnly = true;
			Preview->SetupAttachment(RootComponent);
			Preview->RegisterComponent();

			if (Part.Material)
			{
				Preview->SetMaterial(0, Part.Material);
			}

			PreviewMeshes.Add(Preview);
		}
	}

	// Update connection visualization
	UpdateConnectionVisualization();
}

FColor AConstrainedGroupSpawner::GetColorForConstraintType(EBConstraintType Type) const
{
	switch (Type)
	{
	case EBConstraintType::Fixed:
		return FixedConstraintColor;
	case EBConstraintType::Hinge:
		return HingeConstraintColor;
	case EBConstraintType::Point:
		return PointConstraintColor;
	case EBConstraintType::Distance:
		return DistanceConstraintColor;
	case EBConstraintType::Slider:
		return FColor::Magenta;
	case EBConstraintType::Cone:
		return FColor::Purple;
	default:
		return FColor::White;
	}
}

void AConstrainedGroupSpawner::UpdateConnectionVisualization()
{
#if WITH_EDITORONLY_DATA
	// Clean up old anchor spheres - must unregister before destroy
	for (USphereComponent* Sphere : AnchorPointSpheres)
	{
		if (Sphere && IsValid(Sphere))
		{
			Sphere->UnregisterComponent();
			Sphere->DestroyComponent();
		}
	}
	AnchorPointSpheres.Empty();

	// Clear line batch
	if (ConnectionLinesComponent)
	{
		ConnectionLinesComponent->Flush();
	}

	if (!bShowConnectionLines || !ConnectionLinesComponent)
	{
		return;
	}

	// Draw lines for each connection
	for (int32 i = 0; i < Connections.Num(); ++i)
	{
		const FConstrainedConnection& Conn = Connections[i];

		// Validate indices
		if (!Parts.IsValidIndex(Conn.PartIndexA) || !Parts.IsValidIndex(Conn.PartIndexB))
		{
			continue;
		}

		const FConstrainedBodyPart& PartA = Parts[Conn.PartIndexA];
		const FConstrainedBodyPart& PartB = Parts[Conn.PartIndexB];

		// Calculate local positions
		FVector LocalPosA = PartA.LocalOffset + Conn.LocalAnchorA;
		FVector LocalPosB = PartB.LocalOffset + Conn.LocalAnchorB;

		// Get color based on constraint type
		FColor LineColor = Conn.bIsIntact ? GetColorForConstraintType(Conn.ConstraintType) : BrokenConnectionColor;

		// Draw the connection line
		ConnectionLinesComponent->DrawLine(
			LocalPosA,
			LocalPosB,
			LineColor,
			0, // DepthPriority
			ConnectionLineThickness
		);

		// Draw anchor points as spheres
		if (bShowAnchorPoints)
		{
			// Anchor A
			USphereComponent* SphereA = NewObject<USphereComponent>(this);
			SphereA->SetSphereRadius(AnchorPointRadius);
			SphereA->SetRelativeLocation(LocalPosA);
			SphereA->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			SphereA->bIsEditorOnly = true;
			SphereA->SetVisibility(true);
			SphereA->ShapeColor = AnchorPointColor;
			SphereA->bDrawOnlyIfSelected = false;
			SphereA->SetHiddenInGame(true);
			SphereA->SetupAttachment(RootComponent);
			SphereA->RegisterComponent();
			AnchorPointSpheres.Add(SphereA);

			// Anchor B
			USphereComponent* SphereB = NewObject<USphereComponent>(this);
			SphereB->SetSphereRadius(AnchorPointRadius);
			SphereB->SetRelativeLocation(LocalPosB);
			SphereB->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			SphereB->bIsEditorOnly = true;
			SphereB->SetVisibility(true);
			SphereB->ShapeColor = AnchorPointColor;
			SphereB->bDrawOnlyIfSelected = false;
			SphereB->SetHiddenInGame(true);
			SphereB->SetupAttachment(RootComponent);
			SphereB->RegisterComponent();
			AnchorPointSpheres.Add(SphereB);
		}

		// Draw hinge axis for hinge constraints
		if (Conn.ConstraintType == EBConstraintType::Hinge)
		{
			FVector AxisStart = (LocalPosA + LocalPosB) * 0.5f - Conn.HingeAxis.GetSafeNormal() * 30.0f;
			FVector AxisEnd = (LocalPosA + LocalPosB) * 0.5f + Conn.HingeAxis.GetSafeNormal() * 30.0f;

			ConnectionLinesComponent->DrawLine(
				AxisStart,
				AxisEnd,
				FColor::Blue,
				0,
				ConnectionLineThickness * 0.5f
			);
		}
	}

	// Draw part indices near each part for clarity
	for (int32 i = 0; i < Parts.Num(); ++i)
	{
		const FConstrainedBodyPart& Part = Parts[i];

		// Small vertical line to indicate part number
		FVector Base = Part.LocalOffset + FVector(0, 0, -20);
		FVector Top = Part.LocalOffset + FVector(0, 0, 20);

		ConnectionLinesComponent->DrawLine(
			Base, Top,
			FColor::White,
			0,
			1.0f
		);
	}
#endif
}

#endif
