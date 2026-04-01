// Physics profile for Flecs entity spawning.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsPhysicsProfile.generated.h"

/**
 * Material category for bullet penetration resistance lookup.
 * Maps to GResistanceTable[] in FlecsPenetrationComponents.h.
 * Defined here (UHT-aware header) because it's used by both ECS components and data asset profiles.
 */
UENUM(BlueprintType)
enum class EPenetrationMaterialCategory : uint8
{
	Impenetrable = 0   UMETA(DisplayName = "Impenetrable"),
	Flesh = 1          UMETA(DisplayName = "Flesh"),
	Glass = 2          UMETA(DisplayName = "Glass"),
	Drywall = 3        UMETA(DisplayName = "Drywall"),
	Wood_Thin = 4      UMETA(DisplayName = "Wood (Thin)"),
	Wood_Thick = 5     UMETA(DisplayName = "Wood (Thick)"),
	Sheet_Metal = 6    UMETA(DisplayName = "Sheet Metal"),
	Concrete_Thin = 7  UMETA(DisplayName = "Concrete (Thin)"),
	Metal = 8          UMETA(DisplayName = "Metal"),
	Brick = 9          UMETA(DisplayName = "Brick"),
	Concrete = 10      UMETA(DisplayName = "Concrete"),
	Armor_Plate = 11   UMETA(DisplayName = "Armor Plate"),
	Heavy_Armor = 12   UMETA(DisplayName = "Heavy Armor"),
};

/**
 * Sub-shape definition for compound bodies with per-region materials.
 * Each sub-shape is a box with its own local transform and material category.
 */
USTRUCT(BlueprintType)
struct FSubShapeDefinition
{
	GENERATED_BODY()

	/** Local offset from body origin (UE coords, cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	FVector LocalOffset = FVector::ZeroVector;

	/** Local rotation relative to body */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	FRotator LocalRotation = FRotator::ZeroRotator;

	/** Box half-extents (UE coords, cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	FVector HalfExtents = FVector(50.f);

	/** Penetration material for this sub-shape */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	EPenetrationMaterialCategory Material = EPenetrationMaterialCategory::Wood_Thin;
};

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
	// PENETRATION RESISTANCE
	// ═══════════════════════════════════════════════════════════════

	/** Default material category for this object (used for single-shape bodies and as fallback).
	 *  Impenetrable = no FPenetrationMaterial component added. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration")
	EPenetrationMaterialCategory MaterialCategory = EPenetrationMaterialCategory::Impenetrable;

	/** Max angle from surface normal (degrees) at which bullets can penetrate this material.
	 *  More oblique impacts ricochet or stop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "89"))
	float PenetrationRicochetAngleDeg = 75.f;

	/** Sub-shape definitions for compound bodies with per-region materials.
	 *  If empty, single shape is created using CollisionRadius/HalfHeight.
	 *  If non-empty, a compound body is created from these sub-shapes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Compound")
	TArray<FSubShapeDefinition> CompoundSubShapes;

	// ═══════════════════════════════════════════════════════════════
	// SURFACE INTEGRITY (cumulative degradation)
	// ═══════════════════════════════════════════════════════════════

	/** Enable cumulative surface degradation from repeated hits.
	 *  When enabled, an integrity grid is lazily created on first bullet impact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Surface Integrity")
	bool bDegradable = true;

	/** Base integrity loss per standard hit (25 damage rifle round).
	 *  Higher = material degrades faster. 0.08 = ~13 hits for Wood_Thick, ~50 for Concrete. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Surface Integrity", meta = (ClampMin = "0.001", ClampMax = "1.0", EditCondition = "bDegradable"))
	float BaseDegradeRate = 0.08f;

	/** How much degradation bleeds to neighboring grid cells (0-1).
	 *  0 = no spread, 1 = neighbors degrade equally. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Surface Integrity", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bDegradable"))
	float DegradeSpreadFactor = 0.3f;

	/** Grid columns override (0 = auto from AABB, 1-8 manual). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Surface Integrity", meta = (ClampMin = "0", ClampMax = "8", EditCondition = "bDegradable"))
	uint8 SurfaceGridCols = 0;

	/** Grid rows override (0 = auto from AABB, 1-8 manual). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Surface Integrity", meta = (ClampMin = "0", ClampMax = "8", EditCondition = "bDegradable"))
	uint8 SurfaceGridRows = 0;

	// ─── DEPRECATED ──────────────────────────────────────────────

	/** @deprecated Use MaterialCategory instead. Kept for asset migration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration|Deprecated", meta = (ClampMin = "0", ClampMax = "100"))
	float MaterialResistance = 0.f;

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	bool IsSphere() const { return CollisionHalfHeight <= 0.f; }
	bool IsCapsule() const { return CollisionHalfHeight > 0.f; }
};
