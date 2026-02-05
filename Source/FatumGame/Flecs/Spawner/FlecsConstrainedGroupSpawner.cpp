
#include "FlecsConstrainedGroupSpawner.h"
#include "FlecsGameplayLibrary.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsStaticComponents.h"
#include "FlecsInstanceComponents.h"
#include "BarrageSpawnUtils.h"
#include "FlecsRenderManager.h"
#include "BarrageDispatch.h"
#include "Components/BillboardComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "DrawDebugHelpers.h"
#include "Skeletonize.h"
#include <atomic>

AFlecsConstrainedGroupSpawner::AFlecsConstrainedGroupSpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	// Root component
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

#if WITH_EDITORONLY_DATA
	// Billboard for visibility in editor
	SpriteComponent = CreateDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (SpriteComponent)
	{
		SpriteComponent->SetupAttachment(RootComponent);
		SpriteComponent->bIsScreenSizeScaled = true;
	}
#endif
}

void AFlecsConstrainedGroupSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (bSpawnOnBeginPlay)
	{
		SpawnGroup();

		if (bDestroyAfterSpawn)
		{
			Destroy();
		}
	}
}

FFlecsGroupSpawnResult AFlecsConstrainedGroupSpawner::SpawnGroup()
{
	SpawnResult = FFlecsGroupSpawnResult();

	if (!ValidateConfiguration())
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsConstrainedGroupSpawner '%s': Invalid configuration!"), *GetName());
		return SpawnResult;
	}

	UWorld* World = GetWorld();
	if (!World) return SpawnResult;

	UBarrageDispatch* Barrage = World->GetSubsystem<UBarrageDispatch>();
	UFlecsRenderManager* Renderer = UFlecsRenderManager::Get(World);
	UFlecsArtillerySubsystem* FlecsSubsystem = World->GetSubsystem<UFlecsArtillerySubsystem>();

	if (!Barrage || !FlecsSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("FlecsConstrainedGroupSpawner '%s': Missing subsystems!"), *GetName());
		return SpawnResult;
	}

	FTransform GroupTransform = GetActorTransform();
	SpawnResult.ElementKeys.Reserve(Elements.Num());
	SpawnResult.ConstraintKeys.Reserve(Constraints.Num());

	static std::atomic<uint32> GElementCounter{0};

	// ═══════════════════════════════════════════════════════════════
	// PHASE 1: Spawn all elements
	// ═══════════════════════════════════════════════════════════════

	for (int32 i = 0; i < Elements.Num(); ++i)
	{
		const FFlecsGroupElement& Element = Elements[i];

		if (!Element.Mesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("FlecsConstrainedGroupSpawner '%s': Element %d has no mesh!"), *GetName(), i);
			SpawnResult.ElementKeys.Add(FSkeletonKey());
			continue;
		}

		// Calculate world transform
		FTransform LocalTransform(Element.LocalRotation, Element.LocalOffset, Element.Scale);
		FTransform WorldTransform = LocalTransform * GroupTransform;

		// Generate unique key
		const uint32 Id = ++GElementCounter;
		FSkeletonKey EntityKey = FSkeletonKey(FORGE_SKELETON_KEY(Id, SKELLY::SFIX_ITEM));

		// Create physics body
		FBarrageSpawnParams Params;
		Params.Mesh = Element.Mesh;
		Params.Material = Element.Material;
		Params.WorldTransform = WorldTransform;
		Params.EntityKey = EntityKey;
		Params.MeshScale = Element.Scale;
		Params.PhysicsLayer = Element.PhysicsLayer;
		Params.bAutoCollider = true;
		Params.bIsMovable = Element.bIsMovable;
		Params.bDestructible = Element.MaxHealth > 0.f;
		Params.Friction = Element.Friction;
		Params.Restitution = Element.Restitution;

		FBarrageSpawnResult BarrageResult = FBarrageSpawnUtils::SpawnEntity(World, Params);
		if (!BarrageResult.bSuccess)
		{
			UE_LOG(LogTemp, Warning, TEXT("FlecsConstrainedGroupSpawner '%s': Failed to spawn element %d"), *GetName(), i);
			SpawnResult.ElementKeys.Add(FSkeletonKey());
			continue;
		}

		SpawnResult.ElementKeys.Add(EntityKey);

		// Queue Flecs entity creation
		float MaxHP = Element.MaxHealth;
		float ArmorVal = Element.Armor;
		UStaticMesh* MeshPtr = Element.Mesh;
		FVector ScaleVal = Element.Scale;

		FlecsSubsystem->EnqueueCommand([FlecsSubsystem, EntityKey, MeshPtr, ScaleVal, MaxHP, ArmorVal]()
		{
			flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
			if (!FlecsWorld) return;

			flecs::entity Entity = FlecsWorld->entity()
				.set<FISMRender>({ MeshPtr, ScaleVal });

			if (MaxHP > 0.f)
			{
				FHealthStatic HealthStatic;
				HealthStatic.MaxHP = MaxHP;
				HealthStatic.Armor = ArmorVal;
				HealthStatic.bDestroyOnDeath = true;

				FHealthInstance HealthInstance;
				HealthInstance.CurrentHP = MaxHP;

				Entity.set<FHealthStatic>(HealthStatic);
				Entity.set<FHealthInstance>(HealthInstance);
				Entity.add<FTagDestructible>();
			}

			FlecsSubsystem->BindEntityToBarrage(Entity, EntityKey);
		});
	}

	// ═══════════════════════════════════════════════════════════════
	// PHASE 2: Create constraints
	// ═══════════════════════════════════════════════════════════════

	for (const FFlecsGroupConstraint& ConstraintDef : Constraints)
	{
		// Validate indices
		if (ConstraintDef.Element1Index >= SpawnResult.ElementKeys.Num() ||
			ConstraintDef.Element2Index >= SpawnResult.ElementKeys.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("FlecsConstrainedGroupSpawner '%s': Invalid constraint indices!"), *GetName());
			SpawnResult.ConstraintKeys.Add(0);
			continue;
		}

		FSkeletonKey Key1 = SpawnResult.ElementKeys[ConstraintDef.Element1Index];
		FSkeletonKey Key2 = SpawnResult.ElementKeys[ConstraintDef.Element2Index];

		if (!Key1.IsValid() || !Key2.IsValid())
		{
			SpawnResult.ConstraintKeys.Add(0);
			continue;
		}

		int64 ConstraintKey = 0;

		switch (ConstraintDef.ConstraintType)
		{
		case EFlecsConstraintType::Fixed:
			ConstraintKey = UFlecsGameplayLibrary::CreateFixedConstraint(
				this, Key1, Key2, ConstraintDef.BreakForce, ConstraintDef.BreakTorque);
			break;

		case EFlecsConstraintType::Hinge:
			{
				// Calculate world anchor
				const FFlecsGroupElement& Elem1 = Elements[ConstraintDef.Element1Index];
				FTransform LocalTransform1(Elem1.LocalRotation, Elem1.LocalOffset, Elem1.Scale);
				FTransform WorldTransform1 = LocalTransform1 * GroupTransform;
				FVector WorldAnchor = WorldTransform1.TransformPosition(ConstraintDef.AnchorOffset1);
				FVector WorldHingeAxis = WorldTransform1.TransformVectorNoScale(ConstraintDef.HingeAxis);

				ConstraintKey = UFlecsGameplayLibrary::CreateHingeConstraint(
					this, Key1, Key2, WorldAnchor, WorldHingeAxis, ConstraintDef.BreakForce);
			}
			break;

		case EFlecsConstraintType::Distance:
			UE_LOG(LogTemp, Warning, TEXT("Spawner Distance: MinDist=%.1f, MaxDist=%.1f, SpringFreq=%.2f, SpringDamp=%.2f, LockRot=%d"),
				ConstraintDef.MinDistance, ConstraintDef.MaxDistance, ConstraintDef.SpringFrequency, ConstraintDef.SpringDamping, ConstraintDef.bLockRotation ? 1 : 0);
			ConstraintKey = UFlecsGameplayLibrary::CreateDistanceConstraint(
				this, Key1, Key2, ConstraintDef.MinDistance, ConstraintDef.MaxDistance, ConstraintDef.BreakForce,
				ConstraintDef.SpringFrequency, ConstraintDef.SpringDamping, ConstraintDef.bLockRotation);
			break;

		case EFlecsConstraintType::Point:
			ConstraintKey = UFlecsGameplayLibrary::CreatePointConstraint(
				this, Key1, Key2, ConstraintDef.BreakForce, ConstraintDef.BreakTorque);
			break;
		}

		SpawnResult.ConstraintKeys.Add(ConstraintKey);
	}

	SpawnResult.bSuccess = true;
	UE_LOG(LogTemp, Log, TEXT("FlecsConstrainedGroupSpawner '%s': Spawned %d elements with %d constraints"),
		*GetName(), SpawnResult.ElementKeys.Num(), SpawnResult.ConstraintKeys.Num());

	return SpawnResult;
}

bool AFlecsConstrainedGroupSpawner::ValidateConfiguration() const
{
	if (Elements.Num() == 0)
	{
		return false;
	}

	// Check all elements have meshes
	for (const FFlecsGroupElement& Element : Elements)
	{
		if (!Element.Mesh)
		{
			return false;
		}
	}

	// Check constraint indices
	for (const FFlecsGroupConstraint& Constraint : Constraints)
	{
		if (Constraint.Element1Index < 0 || Constraint.Element1Index >= Elements.Num())
			return false;
		if (Constraint.Element2Index < 0 || Constraint.Element2Index >= Elements.Num())
			return false;
		if (Constraint.Element1Index == Constraint.Element2Index)
			return false;
	}

	return true;
}

void AFlecsConstrainedGroupSpawner::GenerateChainPreset(UStaticMesh* Mesh, int32 Count, FVector Spacing, float BreakForce)
{
	if (!Mesh || Count < 2) return;

	ClearAll();

	// Create elements
	for (int32 i = 0; i < Count; ++i)
	{
		FFlecsGroupElement Element;
		Element.ElementName = FName(*FString::Printf(TEXT("Link_%d"), i));
		Element.Mesh = Mesh;
		Element.LocalOffset = Spacing * static_cast<float>(i);
		Element.bIsMovable = true;
		Elements.Add(Element);
	}

	// Create constraints between adjacent elements
	for (int32 i = 0; i < Count - 1; ++i)
	{
		FFlecsGroupConstraint Constraint;
		Constraint.Element1Index = i;
		Constraint.Element2Index = i + 1;
		Constraint.ConstraintType = EFlecsConstraintType::Fixed;
		Constraint.BreakForce = BreakForce;
		Constraint.AnchorOffset1 = Spacing * 0.5f;
		Constraint.AnchorOffset2 = -Spacing * 0.5f;
		Constraints.Add(Constraint);
	}

#if WITH_EDITOR
	UpdatePreview();
#endif
}

void AFlecsConstrainedGroupSpawner::GenerateGridPreset(UStaticMesh* Mesh, int32 Rows, int32 Columns, FVector Spacing, float BreakForce)
{
	if (!Mesh || Rows < 1 || Columns < 1) return;

	ClearAll();

	// Create elements in grid
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Col = 0; Col < Columns; ++Col)
		{
			FFlecsGroupElement Element;
			Element.ElementName = FName(*FString::Printf(TEXT("Grid_%d_%d"), Row, Col));
			Element.Mesh = Mesh;
			Element.LocalOffset = FVector(Spacing.X * Col, Spacing.Y * Row, 0.f);
			Element.bIsMovable = true;
			Elements.Add(Element);
		}
	}

	// Create horizontal constraints
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Col = 0; Col < Columns - 1; ++Col)
		{
			int32 Index1 = Row * Columns + Col;
			int32 Index2 = Row * Columns + Col + 1;

			FFlecsGroupConstraint Constraint;
			Constraint.Element1Index = Index1;
			Constraint.Element2Index = Index2;
			Constraint.ConstraintType = EFlecsConstraintType::Fixed;
			Constraint.BreakForce = BreakForce;
			Constraint.AnchorOffset1 = FVector(Spacing.X * 0.5f, 0.f, 0.f);
			Constraint.AnchorOffset2 = FVector(-Spacing.X * 0.5f, 0.f, 0.f);
			Constraints.Add(Constraint);
		}
	}

	// Create vertical constraints
	for (int32 Row = 0; Row < Rows - 1; ++Row)
	{
		for (int32 Col = 0; Col < Columns; ++Col)
		{
			int32 Index1 = Row * Columns + Col;
			int32 Index2 = (Row + 1) * Columns + Col;

			FFlecsGroupConstraint Constraint;
			Constraint.Element1Index = Index1;
			Constraint.Element2Index = Index2;
			Constraint.ConstraintType = EFlecsConstraintType::Fixed;
			Constraint.BreakForce = BreakForce;
			Constraint.AnchorOffset1 = FVector(0.f, Spacing.Y * 0.5f, 0.f);
			Constraint.AnchorOffset2 = FVector(0.f, -Spacing.Y * 0.5f, 0.f);
			Constraints.Add(Constraint);
		}
	}

#if WITH_EDITOR
	UpdatePreview();
#endif
}

void AFlecsConstrainedGroupSpawner::ClearAll()
{
	Elements.Empty();
	Constraints.Empty();

#if WITH_EDITOR
	ClearPreviewMeshes();
#endif
}

#if WITH_EDITOR
void AFlecsConstrainedGroupSpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdatePreview();
}

void AFlecsConstrainedGroupSpawner::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Validate constraint indices
	for (FFlecsGroupConstraint& Constraint : Constraints)
	{
		Constraint.Element1Index = FMath::Clamp(Constraint.Element1Index, 0, FMath::Max(0, Elements.Num() - 1));
		Constraint.Element2Index = FMath::Clamp(Constraint.Element2Index, 0, FMath::Max(0, Elements.Num() - 1));
	}

	UpdatePreview();
}
#endif

#if WITH_EDITORONLY_DATA
void AFlecsConstrainedGroupSpawner::UpdatePreview()
{
	ClearPreviewMeshes();

	if (!bShowPreview) return;

	FTransform GroupTransform = GetActorTransform();

	// Create preview meshes for each element
	for (int32 i = 0; i < Elements.Num(); ++i)
	{
		const FFlecsGroupElement& Element = Elements[i];
		if (!Element.Mesh) continue;

		FTransform LocalTransform(Element.LocalRotation, Element.LocalOffset, Element.Scale);
		FTransform WorldTransform = LocalTransform * GroupTransform;

		UStaticMeshComponent* PreviewMesh = NewObject<UStaticMeshComponent>(this);
		PreviewMesh->SetStaticMesh(Element.Mesh);
		if (Element.Material)
		{
			PreviewMesh->SetMaterial(0, Element.Material);
		}
		PreviewMesh->SetWorldTransform(WorldTransform);
		PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PreviewMesh->SetVisibility(true);
		PreviewMesh->bIsEditorOnly = true;
		PreviewMesh->RegisterComponent();
		PreviewMesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);

		PreviewMeshComponents.Add(PreviewMesh);
	}

	// Draw constraint lines using debug lines (visible in editor)
	if (bShowConstraintLines)
	{
		FlushPersistentDebugLines(GetWorld());

		for (const FFlecsGroupConstraint& Constraint : Constraints)
		{
			if (Constraint.Element1Index >= Elements.Num() || Constraint.Element2Index >= Elements.Num())
				continue;

			const FFlecsGroupElement& Elem1 = Elements[Constraint.Element1Index];
			const FFlecsGroupElement& Elem2 = Elements[Constraint.Element2Index];

			FTransform LocalTransform1(Elem1.LocalRotation, Elem1.LocalOffset, Elem1.Scale);
			FTransform LocalTransform2(Elem2.LocalRotation, Elem2.LocalOffset, Elem2.Scale);

			FVector Pos1 = GroupTransform.TransformPosition(Elem1.LocalOffset);
			FVector Pos2 = GroupTransform.TransformPosition(Elem2.LocalOffset);

			FColor LineColor = (Constraint.BreakForce > 0.f) ? BreakableLineColor : ConstraintLineColor;

			DrawDebugLine(GetWorld(), Pos1, Pos2, LineColor, true, -1.f, 0, 3.f);
		}
	}
}

void AFlecsConstrainedGroupSpawner::ClearPreviewMeshes()
{
	for (UStaticMeshComponent* Mesh : PreviewMeshComponents)
	{
		if (Mesh)
		{
			Mesh->DestroyComponent();
		}
	}
	PreviewMeshComponents.Empty();

	FlushPersistentDebugLines(GetWorld());
}
#endif
