// Projectile profile for Flecs entity spawning.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsProjectileProfile.generated.h"

/**
 * Data Asset defining projectile properties for entity spawning.
 *
 * Used with FEntitySpawnRequest to make an entity behave as a projectile
 * with lifetime, bouncing, and velocity tracking.
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsProjectileProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// MOVEMENT
	// ═══════════════════════════════════════════════════════════════

	/** Default launch speed (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0"))
	float DefaultSpeed = 5000.f;

	/** If true, projectile maintains its initial speed (ignores gravity slowdown) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	bool bMaintainSpeed = false;

	// ═══════════════════════════════════════════════════════════════
	// LIFETIME
	// ═══════════════════════════════════════════════════════════════

	/** Maximum lifetime in seconds (projectile destroyed after this) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lifetime", meta = (ClampMin = "0.1"))
	float Lifetime = 10.f;

	/** Fuse time in seconds (0 = no fuse, detonates on contact only).
	 *  When > 0, projectile detonates after this delay regardless of contact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lifetime", meta = (ClampMin = "0"))
	float FuseTime = 0.f;

	/**
	 * Maximum number of bounces before destruction.
	 * -1 = infinite bounces (only dies by lifetime)
	 * 0 = no bounces (destroyed on first contact)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lifetime", meta = (ClampMin = "-1"))
	int32 MaxBounces = 0;

	/**
	 * Minimum velocity before projectile is considered "stopped" and destroyed.
	 * Only applies if MaxBounces != -1.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lifetime", meta = (ClampMin = "0"))
	float MinVelocity = 50.f;

	// ═══════════════════════════════════════════════════════════════
	// GRACE PERIOD
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Grace period in seconds after spawn/bounce.
	 * During this time, velocity check is skipped (prevents killing during bounce animation).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grace Period", meta = (ClampMin = "0"))
	float GracePeriod = 0.25f;

	/**
	 * If true, projectile can't hit the entity that spawned it during grace period.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grace Period")
	bool bIgnoreOwnerDuringGrace = true;

	// ═══════════════════════════════════════════════════════════════
	// BEHAVIOR
	// ═══════════════════════════════════════════════════════════════

	/** If true, projectile rotates to face movement direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bOrientToVelocity = true;

	/** If true, projectile passes through targets after hitting (penetration) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bPenetrating = false;

	/** Maximum targets that can be penetrated (-1 = infinite) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior", meta = (ClampMin = "-1", EditCondition = "bPenetrating"))
	int32 MaxPenetrations = -1;

	/** Penetration budget in cm of material-equivalent depth.
	 *  Higher = passes through more/thicker obstacles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", EditCondition = "bPenetrating"))
	float PenetrationBudget = 20.f;

	/** Damage loss factor per budget fraction consumed (0-2).
	 *  At 1.0: consuming half the budget loses ~50% damage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "2", EditCondition = "bPenetrating"))
	float PenetrationDamageFalloff = 1.f;

	/** Velocity loss factor per budget fraction consumed (0-1).
	 *  At 0.5: consuming half the budget loses ~25% speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bPenetrating"))
	float PenetrationVelocityFalloff = 0.5f;

	/** Minimum angle from surface normal (degrees) for penetration.
	 *  Impacts more oblique than this ricochet instead (if MaxBounces > 0) or stop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "89", EditCondition = "bPenetrating"))
	float PenetrationRicochetAngleDeg = 70.f;

	/** How much kinetic energy transfers to penetrated objects (impulse for fragmentation).
	 *  0 = no energy transfer (bullet passes clean through).
	 *  1 = full energy transfer (heavy slow rounds).
	 *  Light fast bullets: ~0.1. Heavy slow bullets: ~0.8. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "2", EditCondition = "bPenetrating"))
	float PenetrationImpulseTransfer = 0.3f;

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	bool IsBouncing() const { return MaxBounces != 0; }
	bool IsInfiniteBounce() const { return MaxBounces == -1; }
	int32 GetGraceFrames() const { return FMath::RoundToInt(GracePeriod * 120.f); } // 120Hz
};
