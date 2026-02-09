// BarrageSpawnUtils - Spawn parameters and utilities for Barrage entities.
// Local game module spawn utilities.

#pragma once

#include "CoreMinimal.h"
#include "SkeletonTypes.h"
#include "FBarrageKey.h"
#include "EPhysicsLayer.h"

class UStaticMesh;
class UMaterialInterface;

/**
 * Common spawn parameters for Barrage entities.
 * Used by FlecsEntitySpawner, FlecsSpawnLibrary, etc.
 */
struct FBarrageSpawnParams
{
	// Required
	UStaticMesh* Mesh = nullptr;
	FTransform WorldTransform;
	FSkeletonKey EntityKey;

	// Rendering
	UMaterialInterface* Material = nullptr;
	FVector MeshScale = FVector::OneVector;

	// Physics
	EPhysicsLayer PhysicsLayer = EPhysicsLayer::MOVING;
	bool bAutoCollider = true;
	FVector ManualColliderSize = FVector(100, 100, 100);
	bool bIsMovable = true;
	bool bIsSensor = false;
	FVector InitialVelocity = FVector::ZeroVector;
	float GravityFactor = 1.0f;

	// Surface properties
	float Friction = 0.5f;
	float Restitution = 0.3f;
	float LinearDamping = 0.05f;

	// Behavior flags (used by caller for Flecs tag assignment, not applied here)
	bool bDestructible = false;
	bool bDamagesPlayer = false;
	bool bReflective = false;
};

/**
 * Result of spawning a Barrage entity.
 */
struct FBarrageSpawnResult
{
	FSkeletonKey EntityKey;
	FBarrageKey BarrageKey;
	int32 RenderInstanceIndex = INDEX_NONE;
	bool bSuccess = false;
};

/**
 * Utility class for spawning Barrage entities.
 * Handles physics body creation and render instance registration.
 */
class FBarrageSpawnUtils
{
public:
	/**
	 * Generate a globally unique entity key.
	 * Thread-safe (uses atomic counter internally).
	 *
	 * @param KeyType The SFIX nibble type (default: SFIX_BAR_PRIM for physics entities)
	 */
	static FSkeletonKey GenerateUniqueKey(uint64 KeyType = 0x4000000000000000ULL);

	/**
	 * Spawn a physics entity with the given parameters.
	 * Creates Barrage body + ISM render instance.
	 */
	static FBarrageSpawnResult SpawnEntity(UWorld* World, const FBarrageSpawnParams& Params);

	/**
	 * Calculate collider size from mesh bounds.
	 */
	static FVector CalculateColliderSize(UStaticMesh* Mesh, FVector Scale);
};
