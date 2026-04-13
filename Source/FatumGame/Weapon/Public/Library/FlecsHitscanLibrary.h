// FlecsHitscanLibrary — instant ray-cast weapon delivery.
// Resolves a multi-hit ray through the physics world, invokes ApplyBulletHit per target,
// tracks a local penetration budget, and enqueues a pooled tracer VFX.

#pragma once

#include "CoreMinimal.h"

#include "flecs.h"

class UBarrageDispatch;
class UFlecsNiagaraManager;
struct FWeaponStatic;
struct FDamageStatic;
struct FProjectileStatic;
struct FPenetrationStatic;

class FATUMGAME_API UFlecsHitscanLibrary
{
public:
	/**
	 * Fire a hitscan shot.
	 *
	 * Threading: called from the simulation thread (WeaponFireSystem).
	 * Tracers are enqueued MPSC for game-thread spawn by NiagaraManager.
	 *
	 * @param World           Flecs world.
	 * @param Barrage         Barrage dispatch (required, asserted).
	 * @param Shooter         Character entity that fired (used for body filter + owner check).
	 * @param Weapon          Weapon entity (currently unused; reserved for future per-weapon state).
	 * @param Origin          Ray origin (muzzle position, world cm).
	 * @param Direction       Unit direction vector.
	 * @param WeaponStatic    Weapon static (range, tracer config).
	 * @param DamageStatic    Damage parameters for ApplyBulletHit.
	 * @param ProjStaticForFalloff  Optional — provides distance falloff fields. nullptr disables.
	 * @param PenStaticOrNull Optional — enables penetration across multiple targets. nullptr = first-hit stops.
	 * @param NiagaraMgr      Optional — tracer enqueued only if non-null AND WeaponStatic.TracerEffect set.
	 */
	static void FireHitscan(
		flecs::world& World,
		UBarrageDispatch* Barrage,
		flecs::entity Shooter,
		flecs::entity Weapon,
		const FVector& Origin,
		const FVector& Direction,
		const FWeaponStatic& WeaponStatic,
		const FDamageStatic& DamageStatic,
		const FProjectileStatic* ProjStaticForFalloff,
		const FPenetrationStatic* PenStaticOrNull,
		UFlecsNiagaraManager* NiagaraMgr);
};
