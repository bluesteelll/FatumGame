// FlecsRenderManager - ISM component manager for Barrage entities.
// ISM component manager for Barrage entities.

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
 * Groups entities by mesh for batched rendering (1 draw call per mesh type).
 * Syncs ISM transforms from Barrage physics positions each tick.
 */
UCLASS()
class FATUMGAME_API UFlecsRenderManager : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** Add entity mesh instance, returns instance index */
	int32 AddInstance(UStaticMesh* InMesh, UMaterialInterface* InMaterial, const FTransform& Transform, FSkeletonKey Key);

	/** Remove entity (thread-safe, enqueues for game thread processing) */
	void RemoveInstance(FSkeletonKey Key);

	/** Get manager from world */
	static UFlecsRenderManager* Get(UWorld* World);

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return bHasEntities; }
	virtual TStatId GetStatId() const override;

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	struct FMeshGroup
	{
		TObjectPtr<UInstancedStaticMeshComponent> ISM;

		TMap<FSkeletonKey, int32> KeyToIndex;
		TArray<FSkeletonKey> IndexToKey;

		// Pivot offset to center mesh on physics body
		FVector PivotOffset = FVector::ZeroVector;
	};

	UPROPERTY()
	TObjectPtr<AActor> ManagerActor;

	TMap<UStaticMesh*, FMeshGroup> MeshGroups;
	bool bHasEntities = false;

	/** Thread-safe queue for pending removals (sim thread -> game thread) */
	TQueue<FSkeletonKey, EQueueMode::Mpsc> PendingRemovals;

	UInstancedStaticMeshComponent* GetOrCreateISM(UStaticMesh* InMesh, UMaterialInterface* InMaterial);
	void UpdateTransforms();
	void ProcessPendingRemovals();
	void DoRemoveInstance(FSkeletonKey Key);
};
