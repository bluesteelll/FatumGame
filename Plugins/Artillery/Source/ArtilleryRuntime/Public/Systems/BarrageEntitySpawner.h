// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// BarrageEntitySpawner - Drag & drop physics entity spawner
// Just place, set mesh, done!

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkeletonTypes.h"
#include "EPhysicsLayer.h"
#include "BarrageEntitySpawner.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/**
 * SIMPLE Barrage Entity Spawner - just drag, set mesh, play!
 *
 * HOW TO USE:
 * 1. Drag to level
 * 2. Set Mesh
 * 3. Play!
 *
 * That's it. No Niagara setup, no Data Channels, nothing.
 *
 * All entities with same mesh share ONE draw call automatically.
 * Physics via Jolt, rendering via Instanced Static Mesh.
 */
UCLASS(Blueprintable, BlueprintType)
class ARTILLERYRUNTIME_API ABarrageEntitySpawner : public AActor
{
	GENERATED_BODY()

public:
	ABarrageEntitySpawner();

	// ═══════════════════════════════════════════════════════════════
	// MESH (главное - просто выбери меш!)
	// ═══════════════════════════════════════════════════════════════

	/** The mesh to render. All spawners with same mesh = 1 draw call! */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Entity")
	TObjectPtr<UStaticMesh> Mesh;

	/** Optional material override */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Entity")
	TObjectPtr<UMaterialInterface> Material;

	/** Mesh scale */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Entity")
	FVector MeshScale = FVector::OneVector;

	// ═══════════════════════════════════════════════════════════════
	// PHYSICS
	// ═══════════════════════════════════════════════════════════════

	/** Auto-calculate collider from mesh bounds? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Entity|Physics")
	bool bAutoCollider = true;

	/** Manual collider size (if bAutoCollider = false) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Entity|Physics",
		meta = (EditCondition = "!bAutoCollider"))
	FVector ColliderSize = FVector(100, 100, 100);

	/** Physics layer */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Entity|Physics")
	EPhysicsLayer PhysicsLayer = EPhysicsLayer::MOVING;

	/** Can move? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Entity|Physics")
	bool bIsMovable = true;

	/** Sensor/trigger mode? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Entity|Physics")
	bool bIsSensor = false;

	/** Initial velocity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Entity|Physics")
	FVector InitialVelocity = FVector::ZeroVector;

	/** Gravity (0 = none, 1 = normal) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Entity|Physics")
	float GravityFactor = 1.0f;

	// ═══════════════════════════════════════════════════════════════
	// BEHAVIOR
	// ═══════════════════════════════════════════════════════════════

	/** Destroyed when hit by projectile? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Entity|Behavior")
	bool bDestructible = false;

	/** Damages player on contact? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Entity|Behavior")
	bool bDamagesPlayer = false;

	/** Reflects projectiles? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Entity|Behavior")
	bool bReflective = false;

	/** Destroy spawner after entity created? (recommended) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Entity")
	bool bDestroyAfterSpawn = true;

	// ═══════════════════════════════════════════════════════════════
	// PREVIEW
	// ═══════════════════════════════════════════════════════════════

	/** Show mesh preview in editor? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	bool bShowPreview = true;

	// ═══════════════════════════════════════════════════════════════
	// API
	// ═══════════════════════════════════════════════════════════════

	UFUNCTION(BlueprintPure, Category = "Barrage Entity")
	FSkeletonKey GetEntityKey() const { return EntityKey; }

	/** Spawn from Blueprint/C++ code */
	UFUNCTION(BlueprintCallable, Category = "Barrage Entity", meta = (WorldContext = "WorldContextObject"))
	static FSkeletonKey SpawnEntity(
		UObject* WorldContextObject,
		UStaticMesh* InMesh,
		FTransform Transform,
		FVector InMeshScale = FVector(1, 1, 1),
		EPhysicsLayer InPhysicsLayer = EPhysicsLayer::MOVING,
		bool bInIsMovable = true,
		FVector InVelocity = FVector(0, 0, 0),
		float InGravity = 1.0f
	);

	/** Delete entity */
	UFUNCTION(BlueprintCallable, Category = "Barrage Entity")
	static void DestroyEntity(FSkeletonKey InEntityKey);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UStaticMeshComponent> PreviewMeshComponent;

	FSkeletonKey EntityKey;
	int32 InstanceIndex = INDEX_NONE;

	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void UpdatePreview();
#endif

private:
	FSkeletonKey DoSpawn();
};


/**
 * Manages ISM components for all Barrage entities.
 * Created automatically - you don't touch this.
 */
UCLASS()
class ARTILLERYRUNTIME_API UBarrageRenderManager : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** Add entity mesh instance, returns instance index */
	int32 AddInstance(UStaticMesh* InMesh, UMaterialInterface* InMaterial, const FTransform& Transform, FSkeletonKey Key);

	/** Remove entity */
	void RemoveInstance(FSkeletonKey Key);

	/** Get manager */
	static UBarrageRenderManager* Get(UWorld* World);

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
		TArray<FSkeletonKey> IndexToKey; // For reverse lookup when removing
	};

	UPROPERTY()
	TObjectPtr<AActor> ManagerActor;

	TMap<UStaticMesh*, FMeshGroup> MeshGroups;
	bool bHasEntities = false;

	UInstancedStaticMeshComponent* GetOrCreateISM(UStaticMesh* InMesh, UMaterialInterface* InMaterial);
	void UpdateTransforms();
};
