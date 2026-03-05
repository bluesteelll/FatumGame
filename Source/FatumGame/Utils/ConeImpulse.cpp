// Reusable cone-shaped impulse — applies forces to dynamic bodies and characters in a cone.

#include "ConeImpulse.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FWorldSimOwner.h" // FBCharacterBase (mLocomotionUpdate)
#include "FBPhysicsInputTypes.h"
#include "EPhysicsLayer.h"
#include "PhysicsFilters/FastObjectLayerFilters.h"
#include "FlecsArtillerySubsystem.h" // FCharacterPhysBridge

void ApplyConeImpulse(
	const FConeImpulseParams& Params,
	UBarrageDispatch* Barrage,
	TArray<FCharacterPhysBridge>& CharacterBridges,
	TArray<FConeImpulseHit>* OutHits)
{
	check(Barrage);

	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(Params.HalfAngleDeg));
	const FVector3d Dir = Params.DirectionUE.GetSafeNormal();
	const double RadiusSq = static_cast<double>(Params.Radius) * Params.Radius;
	// Close-range adaptive cone: at 20% of radius, transition from wide cone to configured angle.
	// At point-blank, cos=-0.5 (120° half = 240° total) catches sideways body centers (hinged doors).
	// Compensates for body center ≠ body surface offset on large objects.
	const double InnerRadius = Params.Radius * 0.2;

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
		JPH::BodyFilter NoBodyFilter; // accept all bodies

		uint32 FoundCount = 0;
		TArray<uint32> FoundBodies;
		FoundBodies.Reserve(64);

		Barrage->SphereSearch(
			FBarrageKey(), // no source body to exclude at broadphase level
			Params.OriginUE,
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

			// GetPosition returns FVector3f in UE coords (already converted)
			const FVector3f PosF = FBarragePrimitive::GetPosition(Prim);
			const FVector3d BodyPosUE(PosF.X, PosF.Y, PosF.Z);

			const FVector3d ToTarget = BodyPosUE - Params.OriginUE;
			const double DistSq = ToTarget.SizeSquared();
			if (DistSq > RadiusSq || DistSq < 1.0) continue; // out of range or at origin

			const double Dist = FMath::Sqrt(DistSq);
			const FVector3d DirToTarget = ToTarget / Dist;

			// Adaptive cone: hemisphere at close range, configured angle at distance
			const double CloseRatio = FMath::Clamp(Dist / InnerRadius, 0.0, 1.0);
			const double EffectiveCosAngle = FMath::Lerp(-0.5, static_cast<double>(CosHalfAngle), CloseRatio);
			const double DotResult = FVector3d::DotProduct(DirToTarget, Dir);
			if (DotResult < EffectiveCosAngle) continue;

			// Distance falloff
			const float Falloff = (Params.FalloffExponent > 0.f)
				? FMath::Pow(1.f - static_cast<float>(Dist / Params.Radius), Params.FalloffExponent)
				: 1.f;

			// Impulse in UE coords
			const FVector ImpulseUE = FVector(DirToTarget * Params.ImpulseStrength * Falloff);
			Barrage->AddBodyImpulse(BodyKey, ImpulseUE);

			if (OutHits)
			{
				FConeImpulseHit HitData;
				HitData.BodyKey = Prim->KeyOutOfBarrage;
				HitData.ImpulseUE = ImpulseUE;
				HitData.BodyPositionUE = FVector(BodyPosUE);
				OutHits->Add(HitData);
			}
		}
	}

	// ── Phase 2: Characters via CharacterBridges (CharacterVirtual not in broadphase) ──
	for (FCharacterPhysBridge& Bridge : CharacterBridges)
	{
		if (!Bridge.CachedBody || !FBarragePrimitive::IsNotNull(Bridge.CachedBody)) continue;

		const FVector3f CharPosF = FBarragePrimitive::GetPosition(Bridge.CachedBody);
		const FVector3d CharPosUE(CharPosF.X, CharPosF.Y, CharPosF.Z);

		const bool bIsSelf = (Bridge.CharacterKey == Params.SourceKey);

		if (bIsSelf)
		{
			// Self-knockback: push backward (opposite to cone direction)
			if (!Params.bAffectSelf || Params.SelfImpulseMultiplier <= 0.f) continue;

			const FVector3d SelfImpulseUE = -Dir * Params.ImpulseStrength * Params.SelfImpulseMultiplier;

			// ApplyForce takes UE coords (cm/s) — handles Jolt conversion internally
			FBarragePrimitive::ApplyForce(SelfImpulseUE, Bridge.CachedBody, PhysicsInputType::OtherForce);

			// Zero locomotion to let the impulse take effect
			if (Bridge.CachedFBChar)
			{
				Bridge.CachedFBChar->mLocomotionUpdate = JPH::Vec3::sZero();
			}
			continue;
		}

		// Other characters: cone test
		const FVector3d ToTarget = CharPosUE - Params.OriginUE;
		const double DistSq = ToTarget.SizeSquared();
		if (DistSq > RadiusSq || DistSq < 1.0) continue;

		const double Dist = FMath::Sqrt(DistSq);
		const FVector3d DirToTarget = ToTarget / Dist;

		// Adaptive cone (same as Phase 1)
		const double CloseRatio = FMath::Clamp(Dist / InnerRadius, 0.0, 1.0);
		const double EffectiveCosAngle = FMath::Lerp(-0.5, static_cast<double>(CosHalfAngle), CloseRatio);
		const double DotResult = FVector3d::DotProduct(DirToTarget, Dir);
		if (DotResult < EffectiveCosAngle) continue;

		const float Falloff = (Params.FalloffExponent > 0.f)
			? FMath::Pow(1.f - static_cast<float>(Dist / Params.Radius), Params.FalloffExponent)
			: 1.f;

		const FVector3d ImpulseUE = DirToTarget * Params.ImpulseStrength * Falloff;

		// ApplyForce takes UE coords (cm/s) — handles Jolt conversion internally
		FBarragePrimitive::ApplyForce(ImpulseUE, Bridge.CachedBody, PhysicsInputType::OtherForce);

		if (Bridge.CachedFBChar)
		{
			Bridge.CachedFBChar->mLocomotionUpdate = JPH::Vec3::sZero();
		}
	}
}
