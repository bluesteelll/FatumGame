
#include "FlecsRenderManager.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FlecsRenderManager)

UFlecsRenderManager* UFlecsRenderManager::Get(UWorld* World)
{
	return World ? World->GetSubsystem<UFlecsRenderManager>() : nullptr;
}

void UFlecsRenderManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UFlecsRenderManager::Deinitialize()
{
	MeshGroups.Empty();
	if (ManagerActor)
	{
		ManagerActor->Destroy();
		ManagerActor = nullptr;
	}
	Super::Deinitialize();
}

TStatId UFlecsRenderManager::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFlecsRenderManager, STATGROUP_Tickables);
}

UInstancedStaticMeshComponent* UFlecsRenderManager::GetOrCreateISM(UStaticMesh* InMesh, UMaterialInterface* InMaterial)
{
	if (!InMesh)
	{
		return nullptr;
	}

	FMeshGroup* Group = MeshGroups.Find(InMesh);
	if (Group && Group->ISM)
	{
		return Group->ISM;
	}

	// Create manager actor if needed
	if (!ManagerActor)
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("FlecsRenderManager");
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

	// Calculate pivot offset
	FBoxSphereBounds Bounds = InMesh->GetBounds();
	NewGroup.PivotOffset = -Bounds.Origin;

	return ISM;
}

int32 UFlecsRenderManager::AddInstance(UStaticMesh* InMesh, UMaterialInterface* InMaterial, const FTransform& Transform, FSkeletonKey Key)
{
	UInstancedStaticMeshComponent* ISM = GetOrCreateISM(InMesh, InMaterial);
	if (!ISM)
	{
		return INDEX_NONE;
	}

	FMeshGroup& Group = MeshGroups.FindChecked(InMesh);

	// Prevent duplicate keys
	if (Group.KeyToIndex.Contains(Key))
	{
		return Group.KeyToIndex[Key];
	}

	// Apply pivot offset to center mesh on physics position
	FVector ScaledPivotOffset = Group.PivotOffset * Transform.GetScale3D();
	FVector RotatedPivotOffset = Transform.GetRotation().RotateVector(ScaledPivotOffset);

	FTransform AdjustedTransform = Transform;
	AdjustedTransform.SetLocation(Transform.GetLocation() + RotatedPivotOffset);

	int32 Index = ISM->AddInstance(AdjustedTransform, true);

	// Track
	Group.KeyToIndex.Add(Key, Index);
	if (Group.IndexToKey.Num() <= Index)
	{
		Group.IndexToKey.SetNum(Index + 1);
	}
	Group.IndexToKey[Index] = Key;

	bHasEntities = true;

	return Index;
}

void UFlecsRenderManager::RemoveInstance(FSkeletonKey Key)
{
	PendingRemovals.Enqueue(Key);
}

void UFlecsRenderManager::DoRemoveInstance(FSkeletonKey Key)
{
	for (auto& Pair : MeshGroups)
	{
		FMeshGroup& Group = Pair.Value;

		int32* IndexPtr = Group.KeyToIndex.Find(Key);
		if (IndexPtr && *IndexPtr != INDEX_NONE)
		{
			int32 Index = *IndexPtr;

			if (Group.ISM)
			{
				Group.ISM->RemoveInstance(Index);
			}

			// Update tracking for swapped instance (ISM swaps with last on remove)
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

void UFlecsRenderManager::ProcessPendingRemovals()
{
	FSkeletonKey Key;
	while (PendingRemovals.Dequeue(Key))
	{
		DoRemoveInstance(Key);
	}
}

void UFlecsRenderManager::Tick(float DeltaTime)
{
	UpdateTransforms();
}

void UFlecsRenderManager::UpdateTransforms()
{
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
		TArray<FSkeletonKey> KeysToRemove;

		for (auto& KeyIndex : Group.KeyToIndex)
		{
			FSkeletonKey Key = KeyIndex.Key;
			int32 Index = KeyIndex.Value;

			FBLet Body = Physics->GetShapeRef(Key);
			if (FBarragePrimitive::IsNotNull(Body))
			{
				FVector3f Pos = FBarragePrimitive::GetPosition(Body);
				FQuat4f Rot = FBarragePrimitive::OptimisticGetAbsoluteRotation(Body);

				FTransform CurrentTransform;
				Group.ISM->GetInstanceTransform(Index, CurrentTransform, true);
				FVector Scale = CurrentTransform.GetScale3D();

				FVector ScaledPivotOffset = Group.PivotOffset * Scale;
				FVector RotatedPivotOffset = FQuat(Rot).RotateVector(ScaledPivotOffset);

				FTransform NewTransform;
				NewTransform.SetLocation(FVector(Pos) + RotatedPivotOffset);
				NewTransform.SetRotation(FQuat(Rot));
				NewTransform.SetScale3D(Scale);

				Group.ISM->UpdateInstanceTransform(Index, NewTransform, true, false, true);
				bAnyUpdated = true;
			}
			else
			{
				// Orphaned ISM instance - body destroyed without RemoveInstance
				KeysToRemove.Add(Key);
			}
		}

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
