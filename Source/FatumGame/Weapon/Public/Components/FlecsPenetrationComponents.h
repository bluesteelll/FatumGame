// Penetration components for bullet pass-through mechanics.
// FPenetrationStatic lives in PREFAB (shared), FPenetrationInstance is per-entity (mutable).
// FPenetrationMaterial lives in PREFAB for target entities (walls, destructibles, etc).

#pragma once

#include "CoreMinimal.h"

class UFlecsProjectileProfile;
class UFlecsPhysicsProfile;

// ═══════════════════════════════════════════════════════════════
// PENETRATION STATIC (on projectile prefab)
// ═══════════════════════════════════════════════════════════════

/**
 * Static penetration data - lives in PREFAB, shared by all penetrating projectiles of this type.
 * Only set on entities with bPenetrating=true in ProjectileProfile.
 */
struct FPenetrationStatic
{
	/** Total penetration budget in cm of material-equivalent */
	float PenetrationBudget = 20.f;

	/** Max number of objects to penetrate (-1 = unlimited) */
	int32 MaxPenetrations = -1;

	/** Damage loss factor per budget consumed (0-2). Higher = more damage loss. */
	float DamageFalloffFactor = 1.f;

	/** Velocity loss factor per budget consumed (0-1). Higher = more speed loss. */
	float VelocityFalloffFactor = 0.5f;

	/** Minimum cos(angle) for penetration. Below this, ricochet/stop instead.
	 *  Default 0.34 ~ 70 deg from normal. */
	float RicochetCosAngleThreshold = 0.34f;

	/** Kinetic energy transfer to penetrated objects (fragmentation impulse).
	 *  0 = clean pass-through, 1 = full energy dump. */
	float ImpulseTransferFactor = 0.3f;

	static FPenetrationStatic FromProfile(const UFlecsProjectileProfile* Profile);
};

// ═══════════════════════════════════════════════════════════════
// PENETRATION INSTANCE (per projectile entity, mutable)
// ═══════════════════════════════════════════════════════════════

/**
 * Mutable penetration data - per-entity.
 * Tracks remaining budget and accumulated damage loss as the projectile passes through objects.
 */
struct FPenetrationInstance
{
	/** Remaining penetration budget */
	float RemainingBudget = 20.f;

	/** Number of objects penetrated so far */
	int32 PenetrationCount = 0;

	/** Accumulated damage multiplier (compounds with each penetration) */
	float CurrentDamageMultiplier = 1.f;

	/** Entity ID of the last penetrated target. Prevents BounceCollisionSystem from
	 *  killing the bullet on spurious re-contacts with the SAME target from the same StepWorld.
	 *  Reset to 0 when the bullet contacts a DIFFERENT entity. */
	uint64 LastPenetratedTargetId = 0;
};

// ═══════════════════════════════════════════════════════════════
// PENETRATION MATERIAL (on target entity prefab)
// ═══════════════════════════════════════════════════════════════

/**
 * Material resistance data - lives in PREFAB for target entities.
 * Determines how much penetration budget is consumed when a bullet passes through.
 * Only added when PhysicsProfile::MaterialResistance > 0.
 */
struct FPenetrationMaterial
{
	/** Material resistance multiplier. Higher = harder to penetrate.
	 *  Examples: glass=0.2, wood=0.5, metal=3.0, concrete=4.0 */
	float MaterialResistance = 1.f;

	/** Minimum cos(angle) for penetration on this material.
	 *  Default 0.26 ~ 75 deg from normal. */
	float RicochetCosAngleThreshold = 0.26f;

	static FPenetrationMaterial FromProfile(const UFlecsPhysicsProfile* Profile);
};

// FTagCollisionPenetration is defined in FlecsBarrageComponents.h alongside other collision tags
