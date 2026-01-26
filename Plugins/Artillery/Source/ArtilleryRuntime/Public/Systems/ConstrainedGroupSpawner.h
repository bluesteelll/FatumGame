// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// ConstrainedGroupSpawner - Spawn groups of physics bodies connected by breakable constraints

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkeletonTypes.h"
#include "EPhysicsLayer.h"
#include "FBConstraintParams.h"
#include "ConstrainedGroupSpawner.generated.h"

class UStaticMesh;
class UMaterialInterface;

/**
 * Definition of a single physics body within a constrained group.
 */
USTRUCT(BlueprintType)
struct FConstrainedBodyPart
{
	GENERATED_BODY()

	/** Display name for this part (for editor clarity) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FName PartName = NAME_None;

	/** Mesh to render for this part */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	TObjectPtr<UStaticMesh> Mesh;

	/** Optional material override */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	TObjectPtr<UMaterialInterface> Material;

	/** Local offset from spawner origin */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FVector LocalOffset = FVector::ZeroVector;

	/** Local rotation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FRotator LocalRotation = FRotator::ZeroRotator;

	/** Scale of mesh and collider */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FVector Scale = FVector::OneVector;

	/** Auto-calculate collider from mesh? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part|Physics")
	bool bAutoCollider = true;

	/** Manual collider size (if bAutoCollider = false) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part|Physics",
		meta = (EditCondition = "!bAutoCollider"))
	FVector ColliderSize = FVector(100, 100, 100);

	/** Is this part movable? (Usually true for breakable objects) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part|Physics")
	bool bIsMovable = true;

	/** Initial velocity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part|Physics")
	FVector InitialVelocity = FVector::ZeroVector;

	/** Gravity factor (0 = none, 1 = normal) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part|Physics")
	float GravityFactor = 1.0f;

	// === Behavior Tags ===

	/** Destroyed when hit by projectile? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part|Behavior")
	bool bDestructible = false;

	/** Damages player on contact? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part|Behavior")
	bool bDamagesPlayer = false;

	/** Reflects projectiles? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part|Behavior")
	bool bReflective = false;

	// === Runtime state (not for editing) ===

	/** Physics body key - assigned at spawn */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Runtime")
	FSkeletonKey BodyKey;

	/** Barrage key - for internal use */
	FBarrageKey BarrageKey;

	/** Instance index for rendering */
	int32 InstanceIndex = INDEX_NONE;
};

/**
 * Definition of a constraint (connection) between two body parts.
 */
USTRUCT(BlueprintType)
struct FConstrainedConnection
{
	GENERATED_BODY()

	/** Index of first part in the Parts array */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection",
		meta = (ClampMin = "0"))
	int32 PartIndexA = 0;

	/** Index of second part in the Parts array */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection",
		meta = (ClampMin = "0"))
	int32 PartIndexB = 1;

	/** Type of constraint */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection")
	EBConstraintType ConstraintType = EBConstraintType::Fixed;

	/** Local anchor point on Part A (relative to part's local offset) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection")
	FVector LocalAnchorA = FVector::ZeroVector;

	/** Local anchor point on Part B (relative to part's local offset) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection")
	FVector LocalAnchorB = FVector::ZeroVector;

	/** Force threshold to break this connection (Newtons). 0 = unbreakable */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection|Breaking",
		meta = (ClampMin = "0"))
	float BreakForce = 5000.0f;

	/** Torque threshold to break this connection (Nm). 0 = unbreakable */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection|Breaking",
		meta = (ClampMin = "0"))
	float BreakTorque = 1000.0f;

	// === Hinge-specific ===

	/** Axis of rotation for hinge (local space) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection|Hinge",
		meta = (EditCondition = "ConstraintType == EBConstraintType::Hinge"))
	FVector HingeAxis = FVector(0, 0, 1);

	/** Enable angle limits */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection|Hinge",
		meta = (EditCondition = "ConstraintType == EBConstraintType::Hinge"))
	bool bHingeLimits = false;

	/** Min/max angles in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection|Hinge",
		meta = (EditCondition = "ConstraintType == EBConstraintType::Hinge && bHingeLimits"))
	float MinAngle = -90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection|Hinge",
		meta = (EditCondition = "ConstraintType == EBConstraintType::Hinge && bHingeLimits"))
	float MaxAngle = 90.0f;

	// === Distance-specific ===

	/** Min distance (0 = no min) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection|Distance",
		meta = (EditCondition = "ConstraintType == EBConstraintType::Distance", ClampMin = "0"))
	float MinDistance = 0.0f;

	/** Max distance (0 = auto from initial) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection|Distance",
		meta = (EditCondition = "ConstraintType == EBConstraintType::Distance", ClampMin = "0"))
	float MaxDistance = 0.0f;

	/** Spring frequency (0 = rigid) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection|Distance",
		meta = (EditCondition = "ConstraintType == EBConstraintType::Distance", ClampMin = "0"))
	float SpringFrequency = 0.0f;

	// === Runtime ===

	/** Constraint key - assigned at spawn */
	FBarrageConstraintKey ConstraintKey;

	/** Is this connection still intact? */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Runtime")
	bool bIsIntact = true;
};

/**
 * Event fired when a connection breaks
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnConnectionBroken,
	int32, ConnectionIndex,
	int32, PartIndexA,
	int32, PartIndexB);

/**
 * Spawns a group of physics bodies connected by breakable constraints.
 *
 * HOW TO USE:
 * 1. Drag to level
 * 2. Add Parts (each part = one physics body with mesh)
 * 3. Add Connections between parts
 * 4. Set BreakForce on connections
 * 5. Play!
 *
 * Example: A destructible crate made of 6 panels
 * - 6 Parts (top, bottom, 4 sides)
 * - 12 Connections (edges between panels)
 * - When hit hard enough, panels break apart!
 */
UCLASS(Blueprintable, BlueprintType)
class ARTILLERYRUNTIME_API AConstrainedGroupSpawner : public AActor
{
	GENERATED_BODY()

public:
	AConstrainedGroupSpawner();

	// ═══════════════════════════════════════════════════════════════
	// PARTS - The physics bodies
	// ═══════════════════════════════════════════════════════════════

	/** All body parts in this group */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constrained Group")
	TArray<FConstrainedBodyPart> Parts;

	// ═══════════════════════════════════════════════════════════════
	// CONNECTIONS - Constraints between parts
	// ═══════════════════════════════════════════════════════════════

	/** Connections (constraints) between parts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constrained Group")
	TArray<FConstrainedConnection> Connections;

	// ═══════════════════════════════════════════════════════════════
	// PHYSICS SETTINGS
	// ═══════════════════════════════════════════════════════════════

	/** Physics layer for all parts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constrained Group|Physics")
	EPhysicsLayer PhysicsLayer = EPhysicsLayer::MOVING;

	/** Auto-check for broken constraints each tick? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constrained Group|Physics")
	bool bAutoProcessBreaking = true;

	// ═══════════════════════════════════════════════════════════════
	// DEFAULT BEHAVIOR (applied to all parts unless overridden)
	// ═══════════════════════════════════════════════════════════════

	/** Default: All parts are destructible (can be overridden per-part) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constrained Group|Behavior")
	bool bDefaultDestructible = false;

	/** Default: All parts damage player on contact */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constrained Group|Behavior")
	bool bDefaultDamagesPlayer = false;

	/** Default: All parts reflect projectiles */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constrained Group|Behavior")
	bool bDefaultReflective = false;

	/** Default gravity factor for all parts (0 = none, 1 = normal) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constrained Group|Behavior")
	float DefaultGravityFactor = 1.0f;

	// ═══════════════════════════════════════════════════════════════
	// BEHAVIOR
	// ═══════════════════════════════════════════════════════════════

	/** Destroy this actor after spawning? (Parts continue to exist) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constrained Group")
	bool bDestroyAfterSpawn = false;

	/** Destroy parts when all their connections break? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constrained Group")
	bool bDestroyLooseParts = false;

	// ═══════════════════════════════════════════════════════════════
	// PREVIEW
	// ═══════════════════════════════════════════════════════════════

	/** Show preview meshes in editor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	bool bShowPreview = true;

	/** Show connection lines in editor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	bool bShowConnectionLines = true;

	/** Show anchor points as spheres */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	bool bShowAnchorPoints = true;

	/** Line thickness for connections */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview", meta = (ClampMin = "1", ClampMax = "10"))
	float ConnectionLineThickness = 3.0f;

	/** Anchor point sphere radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview", meta = (ClampMin = "1", ClampMax = "50"))
	float AnchorPointRadius = 8.0f;

	/** Color for Fixed constraints */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview|Colors")
	FColor FixedConstraintColor = FColor::Green;

	/** Color for Hinge constraints */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview|Colors")
	FColor HingeConstraintColor = FColor::Yellow;

	/** Color for Point constraints */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview|Colors")
	FColor PointConstraintColor = FColor::Cyan;

	/** Color for Distance constraints */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview|Colors")
	FColor DistanceConstraintColor = FColor::Orange;

	/** Color for broken connections (runtime) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview|Colors")
	FColor BrokenConnectionColor = FColor::Red;

	/** Color for anchor points */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview|Colors")
	FColor AnchorPointColor = FColor::White;

	// ═══════════════════════════════════════════════════════════════
	// EVENTS
	// ═══════════════════════════════════════════════════════════════

	/** Called when a connection breaks */
	UPROPERTY(BlueprintAssignable, Category = "Constrained Group|Events")
	FOnConnectionBroken OnConnectionBroken;

	// ═══════════════════════════════════════════════════════════════
	// API
	// ═══════════════════════════════════════════════════════════════

	/** Get skeleton key for a specific part */
	UFUNCTION(BlueprintPure, Category = "Constrained Group")
	FSkeletonKey GetPartKey(int32 PartIndex) const;

	/** Get all part keys */
	UFUNCTION(BlueprintPure, Category = "Constrained Group")
	TArray<FSkeletonKey> GetAllPartKeys() const;

	/** Manually break a connection */
	UFUNCTION(BlueprintCallable, Category = "Constrained Group")
	bool BreakConnection(int32 ConnectionIndex);

	/** Break all connections to a specific part */
	UFUNCTION(BlueprintCallable, Category = "Constrained Group")
	int32 BreakAllConnectionsToPart(int32 PartIndex);

	/** Check if a connection is still intact */
	UFUNCTION(BlueprintPure, Category = "Constrained Group")
	bool IsConnectionIntact(int32 ConnectionIndex) const;

	/** Get number of intact connections for a part */
	UFUNCTION(BlueprintPure, Category = "Constrained Group")
	int32 GetIntactConnectionCount(int32 PartIndex) const;

	/** Apply impulse to a specific part */
	UFUNCTION(BlueprintCallable, Category = "Constrained Group")
	void ApplyImpulseToPart(int32 PartIndex, FVector Impulse);

	/** Destroy all parts and connections */
	UFUNCTION(BlueprintCallable, Category = "Constrained Group")
	void DestroyAllParts();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void UpdatePreview();
	void UpdateConnectionVisualization();
	FColor GetColorForConstraintType(EBConstraintType Type) const;
#endif

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USceneComponent> RootSceneComponent;

	/** Preview mesh components (editor only) */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PreviewMeshes;

#if WITH_EDITORONLY_DATA
	/** Line batch component for connection visualization */
	UPROPERTY()
	TObjectPtr<class ULineBatchComponent> ConnectionLinesComponent;

	/** Sphere components for anchor point visualization */
	UPROPERTY()
	TArray<TObjectPtr<class USphereComponent>> AnchorPointSpheres;
#endif

private:
	bool bSpawned = false;

	/** Create all physics bodies */
	void SpawnBodies();

	/** Create all constraints between bodies */
	void CreateConstraints();

	/** Check for broken constraints and fire events */
	void ProcessBreaking();

	/** Clean up a destroyed part */
	void CleanupPart(int32 PartIndex);
};
