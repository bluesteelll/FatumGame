// FlecsRenderManager - ISM component manager for Barrage entities.
// Driven by UFlecsArtillerySubsystem::Tick() for guaranteed ordering:
//   1. UpdateTransforms (sync existing ISMs to physics)
//   2. ProcessPendingProjectileSpawns (add new ISMs at correct positions)
// This ensures new ISM positions are never overwritten by stale physics data.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SkeletonTypes.h"
#include "FlecsRenderManager.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/**
 * Manages ISM components for all Barrage entities.
 * Groups entities by (mesh, material) pair for batched rendering.
 * Same mesh with different materials gets separate ISM components.
 *
 * NOT self-ticking — driven by UFlecsArtillerySubsystem to control update ordering.
 */
UCLASS()
class FATUMGAME_API UFlecsRenderManager : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Add entity mesh instance, returns instance index */
	int32 AddInstance(UStaticMesh* InMesh, UMaterialInterface* InMaterial, const FTransform& Transform, FSkeletonKey Key);

	/** Remove entity (thread-safe, enqueues for game thread processing) */
	void RemoveInstance(FSkeletonKey Key);

	/**
	 * Interpolate all ISM transforms between two sim-tick physics states.
	 * @param Alpha Interpolation factor [0,1] between previous and current sim tick.
	 * @param CurrentSimTick Monotonic sim tick counter for detecting new physics states.
	 */
	void UpdateTransforms(float Alpha, uint64 CurrentSimTick);

	/** Get manager from world */
	static UFlecsRenderManager* Get(UWorld* World);

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	/** Per-entity transform state for interpolation between sim ticks. */
	struct FEntityTransformState
	{
		FVector PrevPosition = FVector::ZeroVector;
		FQuat   PrevRotation = FQuat::Identity;
		FVector CurrPosition = FVector::ZeroVector;
		FQuat   CurrRotation = FQuat::Identity;
		uint64  LastUpdateTick = 0;
		bool    bJustSpawned = true;
	};

	/** Key for ISM grouping: (mesh, material) pair. nullptr material = mesh defaults. */
	struct FMeshMaterialKey
	{
		UStaticMesh* Mesh = nullptr;
		UMaterialInterface* Material = nullptr;

		bool operator==(const FMeshMaterialKey& Other) const
		{
			return Mesh == Other.Mesh && Material == Other.Material;
		}

		friend uint32 GetTypeHash(const FMeshMaterialKey& Key)
		{
			return HashCombine(GetTypeHash(Key.Mesh), GetTypeHash(Key.Material));
		}
	};

	struct FMeshGroup
	{
		TObjectPtr<UInstancedStaticMeshComponent> ISM;

		TMap<FSkeletonKey, int32> KeyToIndex;
		TArray<FSkeletonKey> IndexToKey;

		/** Per-entity interpolation state (keyed by same SkeletonKey as KeyToIndex). */
		TMap<FSkeletonKey, FEntityTransformState> TransformStates;

		// Pivot offset to center mesh on physics body
		FVector PivotOffset = FVector::ZeroVector;
	};

	UPROPERTY()
	TObjectPtr<AActor> ManagerActor;

	TMap<FMeshMaterialKey, FMeshGroup> MeshGroups;

	/** Thread-safe queue for pending removals (sim thread -> game thread) */
	TQueue<FSkeletonKey, EQueueMode::Mpsc> PendingRemovals;

	UInstancedStaticMeshComponent* GetOrCreateISM(UStaticMesh* InMesh, UMaterialInterface* InMaterial);
	void ProcessPendingRemovals();
	void DoRemoveInstance(FSkeletonKey Key);
};
