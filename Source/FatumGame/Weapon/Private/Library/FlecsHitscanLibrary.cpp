// FlecsHitscanLibrary — instant ray-cast weapon delivery.

#include "Library/FlecsHitscanLibrary.h"

#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FBarrageRayHit.h"
#include "IsolatedJoltIncludes.h"
#include "EPhysicsLayer.h"
#include "PhysicsFilters/FastObjectLayerFilters.h"

#include "FlecsBarrageComponents.h"
#include "FlecsNiagaraManager.h"
#include "FlecsWeaponComponents.h"
#include "FlecsProjectileComponents.h"
#include "FlecsPenetrationComponents.h"
#include "FlecsHealthComponents.h"

#include "Library/FlecsBulletHitLibrary.h"

namespace
{
	/** Body-filter that excludes the shooter's physics body(ies) from ray casts.
	 *  Inline allocator — characters have one Barrage body in practice; room for a few more. */
	class FShooterExcludeFilter final : public JPH::BodyFilter
	{
	public:
		TArray<JPH::BodyID, TInlineAllocator<4>> Excluded;

		virtual bool ShouldCollide(const JPH::BodyID& inBodyID) const override
		{
			for (const JPH::BodyID& E : Excluded)
			{
				if (inBodyID == E) return false;
			}
			return true;
		}

		virtual bool ShouldCollideLocked(const JPH::Body& /*inBody*/) const override
		{
			return true;
		}
	};
}

void UFlecsHitscanLibrary::FireHitscan(
	flecs::world& World,
	UBarrageDispatch* Barrage,
	flecs::entity Shooter,
	flecs::entity /*Weapon*/,
	const FVector& Origin,
	const FVector& Direction,
	const FWeaponStatic& WeaponStatic,
	const FDamageStatic& DamageStatic,
	const FProjectileStatic* ProjStaticForFalloff,
	const FPenetrationStatic* PenStaticOrNull,
	UFlecsNiagaraManager* NiagaraMgr)
{
	checkf(Barrage, TEXT("FireHitscan: Barrage dispatch is null"));

	FVector UnitDir = Direction.GetSafeNormal();
	if (UnitDir.IsNearlyZero()) return;

	const float Range = FMath::Max(WeaponStatic.HitscanRange, 1.f);

	// ── Filters ──
	auto BPFilter = Barrage->GetDefaultBroadPhaseLayerFilter(Layers::CAST_QUERY);
	FastExcludeObjectLayerFilter ObjFilter({
		EPhysicsLayer::PROJECTILE,
		EPhysicsLayer::ENEMYPROJECTILE,
		EPhysicsLayer::DEBRIS
	});

	FShooterExcludeFilter ShooterFilter;
	if (Shooter.is_valid())
	{
		if (const FBarrageBody* ShooterBody = Shooter.try_get<FBarrageBody>())
		{
			if (ShooterBody->IsValid())
			{
				const FBarrageKey BKey = Barrage->GetBarrageKeyFromSkeletonKey(ShooterBody->BarrageKey);
				if (BKey.KeyIntoBarrage != 0)
				{
					const JPH::BodyID Id = Barrage->GetJoltBodyID(BKey);
					if (!Id.IsInvalid())
					{
						ShooterFilter.Excluded.Add(Id);
					}
				}
			}
		}
	}

	// ── Raycast ──
	TArray<FBarrageRayHit> Hits;
	Hits.Reserve(16);
	Barrage->CastRayAllHits(Origin, UnitDir * Range, BPFilter, ObjFilter, ShooterFilter, Hits);

	// ── Penetration state (local — mirrors FPenetrationInstance layout) ──
	float RemainingBudget = PenStaticOrNull ? PenStaticOrNull->PenetrationBudget : 0.f;
	float CurDamageMul = 1.f;
	int32 PenCount = 0;
	uint64 LastPenetratedTargetId = 0;

	FVector TracerEnd = Origin + UnitDir * Range;
	const uint64 ShooterId = Shooter.is_valid() ? Shooter.id() : 0;

	for (const FBarrageRayHit& Hit : Hits)
	{
		// Resolve BodyID → FBarrageKey → Prim → FlecsEntity
		const FBarrageKey HitKey = Barrage->GenerateBarrageKeyFromBodyId(Hit.BodyIDValue);
		FBLet HitPrim = Barrage->GetShapeRef(HitKey);
		flecs::entity Target;
		if (FBarragePrimitive::IsNotNull(HitPrim))
		{
			const uint64 FlecsId = HitPrim->GetFlecsEntity();
			if (FlecsId != 0)
			{
				Target = World.entity(FlecsId);
			}
		}

		// Static geometry (no flecs entity) — stop at first hit.
		if (!Target.is_valid() || !Target.is_alive())
		{
			TracerEnd = Hit.ContactPoint;
			break;
		}

		// ── Build context ──
		FBulletHitContext Ctx;
		Ctx.ShooterEntityId = ShooterId;
		Ctx.TargetEntityId  = Target.id();
		Ctx.ImpactPoint     = Hit.ContactPoint;
		Ctx.ImpactNormal    = Hit.ContactNormal;
		Ctx.IncomingDir     = UnitDir;
		Ctx.SpawnPosition   = Origin;
		Ctx.IncomingSpeed   = 0.f;

		Ctx.BaseDamage       = DamageStatic.Damage;
		Ctx.StructuralDamage = DamageStatic.StructuralDamage;
		Ctx.CritChance       = DamageStatic.CritChance;
		Ctx.CritMultiplier   = DamageStatic.CritMultiplier;
		Ctx.DamageType       = DamageStatic.DamageType;

		if (ProjStaticForFalloff)
		{
			Ctx.DamageFalloffStart  = ProjStaticForFalloff->DamageFalloffStart;
			Ctx.InvFalloffRange     = ProjStaticForFalloff->InvFalloffRange;
			Ctx.MinDamageMultiplier = ProjStaticForFalloff->MinDamageMultiplier;
		}

		Ctx.SubShapeIDValue = Hit.SubShapeIDValue;
		Ctx.PenStatic       = PenStaticOrNull;
		Ctx.bApplyImpulse   = true;
		Ctx.ImpulseStrength = WeaponStatic.HitscanImpulseScale;
		Ctx.bCanDegrade     = true;

		if (PenStaticOrNull)
		{
			Ctx.InOutRemainingBudget         = &RemainingBudget;
			Ctx.InOutCurrentDamageMultiplier = &CurDamageMul;
			Ctx.InOutPenetrationCount        = &PenCount;
			Ctx.InOutLastPenetratedTargetId  = &LastPenetratedTargetId;
		}

		const FBulletHitResult Res = UFlecsBulletHitLibrary::ApplyBulletHit(
			World, Barrage, Shooter, Target, Ctx);

		TracerEnd = Res.bPenetrated ? Res.ExitPoint : Hit.ContactPoint;

		// Break conditions
		if (!Res.bPenetrated) break;
		if (PenStaticOrNull && PenStaticOrNull->MaxPenetrations >= 0
			&& PenCount >= PenStaticOrNull->MaxPenetrations) break;
		if (RemainingBudget <= 0.01f) break;
	}

	// ── Tracer VFX ──
	if (NiagaraMgr && WeaponStatic.TracerEffect)
	{
		FPendingNiagaraTracer T;
		T.Start     = Origin;
		T.End       = TracerEnd;
		T.Effect    = WeaponStatic.TracerEffect;
		T.Thickness = WeaponStatic.TracerThickness;
		T.Duration  = WeaponStatic.TracerDuration;
		NiagaraMgr->EnqueueTracer(T);
	}
}
