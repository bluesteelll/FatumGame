// Reusable radial explosion — applies damage + impulse to dynamic bodies and characters in a sphere.

#include "ExplosionUtility.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FWorldSimOwner.h" // FBCharacterBase (mLocomotionUpdate)
#include "FBPhysicsInputTypes.h"
#include "EPhysicsLayer.h"
#include "PhysicsFilters/FastObjectLayerFilters.h"
#include "FlecsArtillerySubsystem.h" // FCharacterPhysBridge
#include "FlecsHealthComponents.h"   // FPendingDamage, FDamageHit
#include "FlecsGameTags.h"           // FTagDead

void ApplyExplosion(
	const FExplosionParams& Params,
	UBarrageDispatch* Barrage,
	TArray<FCharacterPhysBridge>& CharacterBridges,
	flecs::world& World)
{
	check(Barrage);
	if (Params.Radius <= 0.f) return;

	const double RadiusSq = static_cast<double>(Params.Radius) * Params.Radius;

	// LOS filter: raycasts should hit static geometry (walls/floors) but ignore debris and projectiles
	FastExcludeObjectLayerFilter LOSObjFilter({
		EPhysicsLayer::PROJECTILE,
		EPhysicsLayer::ENEMYPROJECTILE,
		EPhysicsLayer::DEBRIS
	});
	auto LOSBPFilter = Barrage->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
	JPH::BodyFilter LOSNoBodyFilter;
	TSharedPtr<FHitResult> LOSHit = MakeShared<FHitResult>();

	// LOS check helper: returns true if epicenter can "see" the target body.
	// TargetBodyId = FoundBodies[i] raw ID (or 0 for characters which aren't in broadphase).
	auto HasLineOfSight = [&](const FVector& TargetPos, double Dist, uint32 TargetBodyId) -> bool
	{
		// CastRay direction = Direction * Distance (NOT unit vector)
		FVector RayDir = (TargetPos - Params.EpicenterUE);
		LOSHit->Reset(1.f, false);
		Barrage->CastRay(
			Params.EpicenterUE, RayDir,
			LOSBPFilter, LOSObjFilter, LOSNoBodyFilter, LOSHit);
		if (!LOSHit->bBlockingHit) return true; // nothing hit = clear LOS

		// If the ray hit the target body itself, LOS is clear
		if (TargetBodyId != 0 && static_cast<uint32>(LOSHit->MyItem) == TargetBodyId)
			return true;

		// For characters (TargetBodyId==0) or unknown bodies: use distance comparison
		const double HitDist = FVector::Dist(Params.EpicenterUE, LOSHit->ImpactPoint);
		return HitDist >= (Dist * 0.9); // 90% of distance tolerance (surface < center)
	};

	// ── Phase 1: Dynamic bodies via SphereSearch ──
	{
		// SphereSearch takes UE coords for Location, Jolt meters for Radius
		const double RadiusJolt = Params.Radius / 100.0;

		// Include MOVING + NON_MOVING (static destructibles) + projectile layers
		FastIncludeObjectLayerFilter ObjFilter({
			EPhysicsLayer::MOVING,
			EPhysicsLayer::NON_MOVING,
			EPhysicsLayer::PROJECTILE,
			EPhysicsLayer::ENEMYPROJECTILE
		});

		auto BPFilter = Barrage->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
		JPH::BodyFilter NoBodyFilter;

		uint32 FoundCount = 0;
		TArray<uint32> FoundBodies;
		FoundBodies.Reserve(64);

		Barrage->SphereSearch(
			FBarrageKey(), // no source body exclusion at broadphase level
			Params.EpicenterUE,
			RadiusJolt,
			BPFilter,
			ObjFilter,
			NoBodyFilter,
			&FoundCount,
			FoundBodies);

		for (uint32 i = 0; i < FoundCount; ++i)
		{
			const FBarrageKey BodyKey = Barrage->GenerateBarrageKeyFromBodyId(FoundBodies[i]);
			FBLet Prim = Barrage->GetShapeRef(BodyKey);
			if (!FBarragePrimitive::IsNotNull(Prim)) continue;

			// Skip character inner bodies — handled separately via CharacterBridges
			if (Prim->Me == FBShape::Character) continue;

			const FVector3f PosF = FBarragePrimitive::GetPosition(Prim);
			const FVector BodyPosUE(PosF.X, PosF.Y, PosF.Z);

			const FVector ToTarget = BodyPosUE - Params.EpicenterUE;
			const double DistSq = ToTarget.SizeSquared();
			if (DistSq > RadiusSq || DistSq < 1.0) continue;

			const double Dist = FMath::Sqrt(DistSq);
			const FVector DirToTarget = ToTarget / Dist;

			// LOS check: skip if wall blocks line of sight
			if (!HasLineOfSight(BodyPosUE, Dist, FoundBodies[i])) continue;

			// Distance falloff: pow(1 - d/R, exponent)
			const float DamageFalloff = (Params.DamageFalloff > 0.f)
				? FMath::Pow(1.f - static_cast<float>(Dist / Params.Radius), Params.DamageFalloff)
				: 1.f;
			const float ImpulseFalloff = (Params.ImpulseFalloff > 0.f)
				? FMath::Pow(1.f - static_cast<float>(Dist / Params.Radius), Params.ImpulseFalloff)
				: 1.f;

			// Impulse with vertical bias
			FVector BiasedDir = (DirToTarget + FVector(0.f, 0.f, Params.VerticalBias)).GetSafeNormal();
			const FVector ImpulseUE = BiasedDir * Params.ImpulseStrength * ImpulseFalloff;
			Barrage->AddBodyImpulse(BodyKey, ImpulseUE);

			// Queue damage if entity has health
			const uint64 FlecsId = Prim->GetFlecsEntity();
			if (FlecsId != 0)
			{
				flecs::entity TargetEntity = World.entity(FlecsId);
				if (TargetEntity.is_valid() && TargetEntity.is_alive() && !TargetEntity.has<FTagDead>())
				{
					// Owner self-damage check
					if (FlecsId == Params.OwnerEntityId && !Params.bDamageOwner) continue;

					if (TargetEntity.has<FHealthInstance>())
					{
						const float FinalDamage = Params.BaseDamage * DamageFalloff;
						if (FinalDamage > 0.f)
						{
							FPendingDamage& Pending = TargetEntity.obtain<FPendingDamage>();
							Pending.AddHit(FinalDamage, Params.OwnerEntityId, Params.DamageType,
								Params.EpicenterUE, false, false);
							TargetEntity.modified<FPendingDamage>();
						}
					}
				}
			}
		}
	}

	// ── Phase 2: Characters via CharacterBridges (CharacterVirtual not in broadphase) ──
	for (FCharacterPhysBridge& Bridge : CharacterBridges)
	{
		if (!Bridge.CachedBody || !FBarragePrimitive::IsNotNull(Bridge.CachedBody)) continue;

		const FVector3f CharPosF = FBarragePrimitive::GetPosition(Bridge.CachedBody);
		const FVector CharPosUE(CharPosF.X, CharPosF.Y, CharPosF.Z);

		const FVector ToTarget = CharPosUE - Params.EpicenterUE;
		const double DistSq = ToTarget.SizeSquared();
		if (DistSq > RadiusSq || DistSq < 1.0) continue;

		const double Dist = FMath::Sqrt(DistSq);
		const FVector DirToTarget = ToTarget / Dist;

		// LOS check: skip if wall blocks line of sight (0 = character, not in broadphase)
		if (!HasLineOfSight(CharPosUE, Dist, 0)) continue;

		const bool bIsSelf = (Bridge.Entity.is_valid() &&
			Bridge.Entity.id() == Params.OwnerEntityId);

		// Distance falloff
		const float DamageFalloff = (Params.DamageFalloff > 0.f)
			? FMath::Pow(1.f - static_cast<float>(Dist / Params.Radius), Params.DamageFalloff)
			: 1.f;
		const float ImpulseFalloff = (Params.ImpulseFalloff > 0.f)
			? FMath::Pow(1.f - static_cast<float>(Dist / Params.Radius), Params.ImpulseFalloff)
			: 1.f;

		// Impulse with vertical bias
		FVector BiasedDir = (DirToTarget + FVector(0.f, 0.f, Params.VerticalBias)).GetSafeNormal();
		const FVector3d ImpulseUE = FVector3d(BiasedDir) * Params.ImpulseStrength * ImpulseFalloff;

		FBarragePrimitive::ApplyForce(ImpulseUE, Bridge.CachedBody, PhysicsInputType::OtherForce);

		// Zero locomotion to let the impulse take effect
		if (Bridge.CachedFBChar)
		{
			Bridge.CachedFBChar->mLocomotionUpdate = JPH::Vec3::sZero();
		}

		// Queue damage
		if (!bIsSelf || Params.bDamageOwner)
		{
			flecs::entity CharEntity = Bridge.Entity;
			if (CharEntity.is_valid() && CharEntity.is_alive() && !CharEntity.has<FTagDead>()
				&& CharEntity.has<FHealthInstance>())
			{
				const float FinalDamage = Params.BaseDamage * DamageFalloff;
				if (FinalDamage > 0.f)
				{
					FPendingDamage& Pending = CharEntity.obtain<FPendingDamage>();
					Pending.AddHit(FinalDamage, Params.OwnerEntityId, Params.DamageType,
						Params.EpicenterUE, false, false);
					CharEntity.modified<FPendingDamage>();
				}
			}
		}
	}
}
