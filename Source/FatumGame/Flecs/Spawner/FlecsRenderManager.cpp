
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
			Group.TransformStates.Remove(Key);
			Group.IndexToKey.SetNum(FMath::Max(0, Group.IndexToKey.Num() - 1));

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

	// Periodic log counter (~1/sec at 60fps)
	static uint32 InterpLogCounter = 0;
	const bool bLogThisFrame = (++InterpLogCounter % 60 == 0);

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
				// Entity added without TransformState (shouldn't happen, but be safe)
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

				UE_LOG(LogTemp, Log, TEXT("INTERP [Snap] Key=%llu Pos=(%.1f,%.1f,%.1f)"),
					static_cast<uint64>(Key), RenderPos.X, RenderPos.Y, RenderPos.Z);
			}
			else
			{
				RenderPos = FMath::Lerp(State->PrevPosition, State->CurrPosition, Alpha);
				RenderRot = FQuat::Slerp(State->PrevRotation, State->CurrRotation, Alpha);

				// Log first entity per group, ~1/sec: shows interpolation + velocity in action
				if (bLogThisFrame && KeyIndex.Key == Group.KeyToIndex.begin()->Key)
				{
					float PrevCurrDist = FVector::Dist(State->PrevPosition, State->CurrPosition);

					// Also log physics velocity direction to diagnose flight path issues
					FVector3f Vel3f = FBarragePrimitive::GetVelocity(Body);
					FVector Vel(Vel3f);
					FVector VelDir = Vel.GetSafeNormal();
					float Speed = Vel.Size();

					UE_LOG(LogTemp, Log, TEXT("INTERP [Lerp] Key=%llu Alpha=%.3f Tick=%llu Prev=(%.1f,%.1f,%.1f) Curr=(%.1f,%.1f,%.1f) Render=(%.1f,%.1f,%.1f) PrevCurrDist=%.2f Vel=(%.3f,%.3f,%.3f) Speed=%.0f Entities=%d"),
						static_cast<uint64>(Key), Alpha, CurrentSimTick,
						State->PrevPosition.X, State->PrevPosition.Y, State->PrevPosition.Z,
						State->CurrPosition.X, State->CurrPosition.Y, State->CurrPosition.Z,
						RenderPos.X, RenderPos.Y, RenderPos.Z,
						PrevCurrDist,
						VelDir.X, VelDir.Y, VelDir.Z, Speed,
						Group.KeyToIndex.Num());
				}
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
			PendingRemovals.Enqueue(Key);
		}

		if (bAnyUpdated)
		{
			Group.ISM->MarkRenderStateDirty();
		}
	}
}
