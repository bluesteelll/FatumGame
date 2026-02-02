// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Physics profile for Flecs entity spawning.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsPhysicsProfile.generated.h"

/**
 * Physics layer for collision filtering.
 */
UENUM(BlueprintType)
enum class EFlecsPhysicsLayer : uint8
{
	Static		UMETA(DisplayName = "Static (non-moving)"),
	Moving		UMETA(DisplayName = "Moving (dynamic)"),
	Projectile	UMETA(DisplayName = "Projectile"),
	Character	UMETA(DisplayName = "Character"),
	Trigger		UMETA(DisplayName = "Trigger (sensor)")
};

/**
 * Data Asset defining physics properties for entity spawning.
 *
 * Used with FEntitySpawnRequest to add physics (Barrage/Jolt body) to an entity.
 * Entities without PhysicsProfile won't have collision or physics simulation.
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsPhysicsProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// COLLISION
	// ═══════════════════════════════════════════════════════════════

	/** Collision sphere/capsule radius in cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision", meta = (ClampMin = "1", ClampMax = "500"))
	float CollisionRadius = 30.f;

	/** Collision height for capsule (0 = sphere) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision", meta = (ClampMin = "0"))
	float CollisionHalfHeight = 0.f;

	/** Physics layer for collision filtering */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	EFlecsPhysicsLayer Layer = EFlecsPhysicsLayer::Moving;

	// ═══════════════════════════════════════════════════════════════
	// DYNAMICS
	// ═══════════════════════════════════════════════════════════════

	/** Mass in kg */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.01", ClampMax = "10000"))
	float Mass = 1.f;

	/** Bounciness (0 = no bounce, 1 = perfect elastic) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0", ClampMax = "1"))
	float Restitution = 0.3f;

	/** Surface friction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0", ClampMax = "2"))
	float Friction = 0.5f;

	/** Linear damping (air resistance) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0", ClampMax = "10"))
	float LinearDamping = 0.01f;

	/** Angular damping (rotation resistance) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0", ClampMax = "10"))
	float AngularDamping = 0.05f;

	/** Gravity multiplier (0 = no gravity, 1 = normal, 2 = double) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0", ClampMax = "5"))
	float GravityFactor = 1.f;

	// ═══════════════════════════════════════════════════════════════
	// BEHAVIOR
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Sensor mode - detects overlaps but doesn't collide.
	 * Use for triggers, pickup zones, etc.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bIsSensor = false;

	/**
	 * Kinematic mode - controlled by code, not physics.
	 * Use for moving platforms, animated objects.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bIsKinematic = false;

	/**
	 * Continuous collision detection - prevents tunneling for fast objects.
	 * More expensive, use only for fast projectiles.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bUseCCD = false;

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	bool IsSphere() const { return CollisionHalfHeight <= 0.f; }
	bool IsCapsule() const { return CollisionHalfHeight > 0.f; }
};
