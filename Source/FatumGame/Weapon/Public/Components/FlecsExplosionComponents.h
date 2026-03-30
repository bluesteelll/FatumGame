// Explosion components for Flecs Prefabs and Entities.
// FExplosionStatic lives in PREFAB (shared), FExplosionContactData is per-entity (transient).

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UNiagaraSystem;
class UFlecsExplosionProfile;

// ═══════════════════════════════════════════════════════════════
// EXPLOSION STATIC
// ═══════════════════════════════════════════════════════════════

/**
 * Static explosion data - lives in PREFAB, shared by all explosive projectiles of this type.
 * Only set on entities that should explode (grenades, rockets, etc).
 * Separate from FDamageStatic — an entity can have both (contact damage + explosion).
 */
struct FExplosionStatic
{
	/** Blast radius (cm) */
	float Radius = 500.f;

	/** Base damage at epicenter */
	float BaseDamage = 50.f;

	/** Radial impulse strength (cm/s) */
	float ImpulseStrength = 2000.f;

	/** Damage falloff exponent: 1.0=linear, 2.0=quadratic, 0.0=flat (full damage everywhere) */
	float DamageFalloff = 1.f;

	/** Impulse falloff exponent */
	float ImpulseFalloff = 1.f;

	/** Upward bias on impulse direction (0=pure radial, 0.5=moderate lift) */
	float VerticalBias = 0.3f;

	/** Offset epicenter along contact normal (cm) to push blast away from surface */
	float EpicenterLift = 5.f;

	/** Can hurt the entity's owner? */
	bool bDamageOwner = false;

	/** Explosion VFX (spawned at detonation point) */
	UNiagaraSystem* ExplosionEffect = nullptr;

	/** Scale for explosion VFX */
	float ExplosionEffectScale = 1.f;

	/** Damage type tag for resistances */
	FGameplayTag DamageType;

	static FExplosionStatic FromProfile(const UFlecsExplosionProfile* Profile);
};

// ═══════════════════════════════════════════════════════════════
// EXPLOSION CONTACT DATA (transient, per-entity)
// ═══════════════════════════════════════════════════════════════

/**
 * Transient data set on an entity at the moment of impact/fuse expiry.
 * Carries the contact normal so the explosion system can offset the epicenter.
 */
struct FExplosionContactData
{
	FVector ContactNormal = FVector::ZeroVector;
};

// ═══════════════════════════════════════════════════════════════
// DETONATION TAG
// ═══════════════════════════════════════════════════════════════

/** Tag added to entities that should explode this tick.
 *  Consumed by ExplosionSystem, which then adds FTagDead. */
struct FTagDetonate {};
