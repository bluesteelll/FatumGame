
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

UInstancedStaticMeshComponent* UFlecsRenderManager::GetOrCreateISM(UStaticMesh* InMesh, UMaterialInterface* InMaterial)
{
	if (!InMesh)
	{
		return nullptr;
	}

	FMeshMaterialKey GroupKey;
	GroupKey.Mesh = InMesh;
	GroupKey.Material = InMaterial;

	FMeshGroup* Group = MeshGroups.Find(GroupKey);
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
		UE_LOG(LogTemp, Log, TEXT("ISM: Created group Mesh='%s' Mat='%s'"),
			*InMesh->GetName(), *InMaterial->GetName());
	}
	else
	{
		// ISM does NOT inherit materials from SetStaticMesh — apply mesh defaults explicitly
		const int32 NumMats = InMesh->GetStaticMaterials().Num();
		for (int32 i = 0; i < NumMats; ++i)
		{
			UMaterialInterface* MeshMat = InMesh->GetMaterial(i);
			if (MeshMat)
			{
				ISM->SetMaterial(i, MeshMat);
			}
		}
		UE_LOG(LogTemp, Log, TEXT("ISM: Created group Mesh='%s' Mat=MeshDefaults(%d slots)"),
			*InMesh->GetName(), NumMats);
	}

	// Store
	FMeshGroup& NewGroup = MeshGroups.Add(GroupKey);
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

	FMeshMaterialKey GroupKey;
	GroupKey.Mesh = InMesh;
	GroupKey.Material = InMaterial;

	FMeshGroup& Group = MeshGroups.FindChecked(GroupKey);

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

	// Init interpolation state — snap, no lerp on first frame
	FEntityTransformState& State = Group.TransformStates.Add(Key);
	State.CurrPosition = State.PrevPosition = Transform.GetLocation();
	State.CurrRotation = State.PrevRotation = Transform.GetRotation();
	State.bJustSpawned = true;

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
			int32 LastIndex = Group.IndexToKey.Num() - 1;
			checkf(Index >= 0 && Index <= LastIndex,
				TEXT("DoRemoveInstance: Index %d out of range [0, %d]"), Index, LastIndex);

			if (Group.ISM)
			{
				// Manual swap-and-pop: copy last instance into removed slot, then remove last (O(1)).
				// ISM::RemoveInstance shifts all indices above — removing LAST avoids any shift.
				if (Index != LastIndex && LastIndex >= 0)
				{
					FTransform LastTransform;
					Group.ISM->GetInstanceTransform(LastIndex, LastTransform, true);
					Group.ISM->UpdateInstanceTransform(Index, LastTransform, true, false, true);
				}
				Group.ISM->RemoveInstance(LastIndex);
			}

			// Mirror swap-and-pop in tracking
			if (Index != LastIndex && LastIndex >= 0)
			{
				FSkeletonKey SwappedKey = Group.IndexToKey[LastIndex];
				Group.KeyToIndex[SwappedKey] = Index;
				Group.IndexToKey[Index] = SwappedKey;
			}

			Group.KeyToIndex.Remove(Key);
			Group.TransformStates.Remove(Key);
			Group.IndexToKey.SetNum(FMath::Max(0, LastIndex));

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

void UFlecsRenderManager::UpdateTransforms(float Alpha, uint64 CurrentSimTick)
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
			if (!FBarragePrimitive::IsNotNull(Body))
			{
				KeysToRemove.Add(Key);
				continue;
			}

			FVector3f Pos3f = FBarragePrimitive::GetPosition(Body);
			FVector PhysPos(Pos3f);

			// Skip NaN positions (can occur during StepWorld)
			if (PhysPos.ContainsNaN())
			{
				continue;
			}

			FQuat4f Rot4f = FBarragePrimitive::OptimisticGetAbsoluteRotation(Body);
			FQuat PhysRot(Rot4f);

			// Update interpolation state
			FEntityTransformState* State = Group.TransformStates.Find(Key);
			if (!State)
			{
				KeysToRemove.Add(Key);
				continue;
			}

			// Detect new sim tick: shift Curr → Prev, update Curr
			if (CurrentSimTick > State->LastUpdateTick)
			{
				State->PrevPosition = State->CurrPosition;
				State->PrevRotation = State->CurrRotation;
				State->CurrPosition = PhysPos;
				State->CurrRotation = PhysRot;
				State->LastUpdateTick = CurrentSimTick;
			}

			// Compute render position
			FVector RenderPos;
			FQuat RenderRot;

			if (State->bJustSpawned)
			{
				// First frame: snap to current physics, no lerp
				RenderPos = State->CurrPosition;
				RenderRot = State->CurrRotation;
				State->bJustSpawned = false;
			}
			else
			{
				RenderPos = FMath::Lerp(State->PrevPosition, State->CurrPosition, Alpha);
				RenderRot = FQuat::Slerp(State->PrevRotation, State->CurrRotation, Alpha);
			}

			// Apply pivot offset AFTER interpolation
			FTransform CurrentTransform;
			Group.ISM->GetInstanceTransform(Index, CurrentTransform, true);
			FVector Scale = CurrentTransform.GetScale3D();

			FVector ScaledPivotOffset = Group.PivotOffset * Scale;
			FVector RotatedPivotOffset = RenderRot.RotateVector(ScaledPivotOffset);

			FTransform NewTransform;
			NewTransform.SetLocation(RenderPos + RotatedPivotOffset);
			NewTransform.SetRotation(RenderRot);
			NewTransform.SetScale3D(Scale);

			Group.ISM->UpdateInstanceTransform(Index, NewTransform, true, false, true);
			bAnyUpdated = true;
		}

		for (const FSkeletonKey& Key : KeysToRemove)
		{
			DoRemoveInstance(Key);
		}

		if (bAnyUpdated)
		{
			Group.ISM->MarkRenderStateDirty();
		}
	}
}
