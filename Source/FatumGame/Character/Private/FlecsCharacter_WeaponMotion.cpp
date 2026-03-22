// Movement-based weapon motion effects for AFlecsCharacter.
// All 6 systems: Walk Bob, Strafe Tilt, Landing Impact, Sprint Pose, Movement Inertia, Footstep Impact.
// Each system computes offsets that layer additively onto weapon mesh transform.

#include "FlecsCharacter.h"
#include "FlecsWeaponProfile.h"
#include "FatumMovementComponent.h"

void AFlecsCharacter::TickWeaponMotion(float DeltaTime)
{
	const UFlecsWeaponProfile* Profile = RecoilState.CachedProfile;
	if (!Profile || !FatumMovement) return;

	// Get character velocity in world space
	const FVector WorldVel = GetVelocity();
	const FRotator ViewRot = GetControlRotation();

	// Project velocity into view-relative axes (forward/right relative to camera yaw)
	const FRotator YawOnlyRot(0.f, ViewRot.Yaw, 0.f);
	const FVector Forward = YawOnlyRot.Vector();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();

	const float ForwardSpeed = FVector::DotProduct(WorldVel, Forward);
	const float RightSpeed = FVector::DotProduct(WorldVel, Right);
	const float HorizontalSpeed = FVector2D(WorldVel.X, WorldVel.Y).Size();

	const bool bSprinting = FatumMovement->IsSprinting();
	const bool bInAir = FatumMovement->IsFalling();
	const float ADSAlpha = RecoilState.ADSAlpha;

	// Resolve movement state for bob multiplier
	const EWeaponMoveState WeaponMoveState = ResolveWeaponMoveState(
		static_cast<uint8>(FatumMovement->GetCurrentMoveMode()),
		static_cast<uint8>(FatumMovement->GetCurrentPosture()));
	const float StateBobMult = Profile->GetStateMultipliers(WeaponMoveState).BobMultiplier;

	// Accumulate position and rotation offsets from all systems
	FVector TotalPosOffset = FVector::ZeroVector;
	FRotator TotalRotOffset = FRotator::ZeroRotator;

	// ═══════════════════════════════════════════════════════════════
	// 1. WALK BOB — Lissajous figure-8 driven by movement speed
	// ═══════════════════════════════════════════════════════════════
	if (Profile->WalkBobAmplitudeH > 0.f || Profile->WalkBobAmplitudeV > 0.f)
	{
		// Speed ratio: 0 when still, 1 at reference speed
		float SpeedRatio = FMath::Clamp(HorizontalSpeed / Profile->WalkBobReferenceSpeed, 0.f, 1.f);

		if (bInAir) SpeedRatio = 0.f;  // no bob while airborne

		float BobMultiplier = StateBobMult;

		// Advance phase proportional to speed
		RecoilState.WalkBobPhase += Profile->WalkBobFrequency * DeltaTime * UE_TWO_PI * SpeedRatio;
		if (RecoilState.WalkBobPhase > UE_TWO_PI * 100.f)
			RecoilState.WalkBobPhase = FMath::Fmod(RecoilState.WalkBobPhase, UE_TWO_PI);

		// Figure-8: horizontal = sin(phase), vertical = -|sin(phase)| (bounce per step)
		float ADSBobScale = FMath::Lerp(1.f, Profile->ADSBobMultiplier, ADSAlpha);
		float BobH = FMath::Sin(RecoilState.WalkBobPhase) * Profile->WalkBobAmplitudeH * SpeedRatio * BobMultiplier * ADSBobScale;
		float BobV = -FMath::Abs(FMath::Sin(RecoilState.WalkBobPhase)) * Profile->WalkBobAmplitudeV * SpeedRatio * BobMultiplier * ADSBobScale;
		float BobRoll = FMath::Sin(RecoilState.WalkBobPhase) * Profile->WalkBobRollAmplitude * SpeedRatio * BobMultiplier * ADSBobScale;

		TotalPosOffset.X += BobH;  // lateral
		TotalPosOffset.Z += BobV;  // vertical
		TotalRotOffset.Roll += BobRoll;
	}

	// ═══════════════════════════════════════════════════════════════
	// 2. STRAFE TILT — weapon rolls when moving laterally
	// ═══════════════════════════════════════════════════════════════
	if (Profile->StrafeTiltAngle > 0.f)
	{
		float TargetTilt = 0.f;
		if (!bInAir && HorizontalSpeed > 10.f)
		{
			// Normalize lateral speed to [-1, 1] based on reference speed
			float LateralRatio = FMath::Clamp(RightSpeed / Profile->WalkBobReferenceSpeed, -1.f, 1.f);
			TargetTilt = LateralRatio * Profile->StrafeTiltAngle;
			TargetTilt *= FMath::Lerp(1.f, Profile->ADSStrafeTiltMultiplier, ADSAlpha);
		}

		RecoilState.CurrentStrafeTilt = FMath::FInterpTo(
			RecoilState.CurrentStrafeTilt, TargetTilt, DeltaTime, Profile->StrafeTiltSpeed);

		TotalRotOffset.Roll += RecoilState.CurrentStrafeTilt;
	}

	// ═══════════════════════════════════════════════════════════════
	// 3. LANDING IMPACT — spring impulse when touching ground
	//    Track fall speed while airborne (velocity is zero on landing frame).
	//    Also: weapon floats up slightly during freefall.
	// ═══════════════════════════════════════════════════════════════
	if (Profile->LandingImpactScale > 0.f)
	{
		if (bInAir)
		{
			// Track max downward speed while airborne
			RecoilState.TrackedFallSpeed = FMath::Max(RecoilState.TrackedFallSpeed, FMath::Abs(WorldVel.Z));

			// Weapon floats up slightly during freefall (weightlessness)
			float FallRatio = FMath::Clamp(FMath::Abs(WorldVel.Z) / 500.f, 0.f, 1.f);
			RecoilState.LandingImpactVelocity.Z += FallRatio * 30.f * DeltaTime;
		}

		// Detect landing: was in air last frame, on ground now
		if (RecoilState.bWasInAir && !bInAir)
		{
			// Use tracked fall speed (not current — already zeroed by physics)
			// TrackedFallSpeed is cm/s, convert to m/s for human-readable scale param
			float FallSpeedMs = RecoilState.TrackedFallSpeed / 100.f;
			float ImpulseVelocity = FallSpeedMs * Profile->LandingImpactScale;
			ImpulseVelocity *= FMath::Lerp(1.f, Profile->ADSLandingMultiplier, ADSAlpha);
			RecoilState.LandingImpactVelocity.Z -= ImpulseVelocity;
			RecoilState.TrackedFallSpeed = 0.f;
		}
		RecoilState.bWasInAir = bInAir;

		// Spring-damper for landing offset
		if (!RecoilState.LandingImpactOffset.IsNearlyZero(0.001f) || !RecoilState.LandingImpactVelocity.IsNearlyZero(0.1f))
		{
			const float lk = Profile->LandingSpringStiffness;
			const float lc = 2.f * Profile->LandingSpringDamping * FMath::Sqrt(lk);

			FVector LandAccel;
			LandAccel.X = 0.f;
			LandAccel.Y = 0.f;
			LandAccel.Z = -lk * RecoilState.LandingImpactOffset.Z - lc * RecoilState.LandingImpactVelocity.Z;

			RecoilState.LandingImpactVelocity += LandAccel * DeltaTime;
			RecoilState.LandingImpactOffset += RecoilState.LandingImpactVelocity * DeltaTime;

			// Snap to zero when settled
			if (RecoilState.LandingImpactOffset.SizeSquared() < 0.0001f && RecoilState.LandingImpactVelocity.SizeSquared() < 0.01f)
			{
				RecoilState.LandingImpactOffset = FVector::ZeroVector;
				RecoilState.LandingImpactVelocity = FVector::ZeroVector;
			}
		}

		TotalPosOffset += RecoilState.LandingImpactOffset;
	}

	// ═══════════════════════════════════════════════════════════════
	// 4. SPRINT POSE — weapon lowers and tilts during sprint
	// ═══════════════════════════════════════════════════════════════
	{
		float TargetAlpha = bSprinting ? 1.f : 0.f;
		RecoilState.SprintPoseAlpha = FMath::FInterpTo(
			RecoilState.SprintPoseAlpha, TargetAlpha, DeltaTime, Profile->SprintTransitionSpeed);

		if (RecoilState.SprintPoseAlpha > 0.001f)
		{
			TotalPosOffset += Profile->SprintPoseOffset * RecoilState.SprintPoseAlpha;
			TotalRotOffset += Profile->SprintPoseRotation * RecoilState.SprintPoseAlpha;
		}
	}

	// ═══════════════════════════════════════════════════════════════
	// 5. MOVEMENT INERTIA — velocity-based spring (physics already provides smooth acceleration)
	//    Target offset = opposite of velocity * scale. Spring chases target.
	//    On stop: target → 0, spring overshoots → weapon "slides" forward, bounces back.
	// ═══════════════════════════════════════════════════════════════
	if (Profile->MovementInertiaScale > 0.f)
	{
		// Target offset: weapon shifts opposite to movement direction
		FVector TargetOffset = FVector::ZeroVector;
		if (!bInAir && HorizontalSpeed > 10.f)
		{
			const float MScale = Profile->MovementInertiaScale * FMath::Lerp(1.f, Profile->ADSMovementInertiaMultiplier, ADSAlpha);
			TargetOffset.X = -RightSpeed * MScale;    // strafe right → weapon shifts left
			TargetOffset.Y = -ForwardSpeed * MScale;   // move forward → weapon shifts back
		}

		// Spring-damper toward target (not toward zero — this is the key difference)
		const float mk = Profile->MovementInertiaStiffness;
		const float mc = 2.f * Profile->MovementInertiaDamping * FMath::Sqrt(mk);

		FVector Displacement = RecoilState.MovementInertiaOffset - TargetOffset;

		FVector MoveAccel;
		MoveAccel.X = -mk * Displacement.X - mc * RecoilState.MovementInertiaVelocity.X;
		MoveAccel.Y = -mk * Displacement.Y - mc * RecoilState.MovementInertiaVelocity.Y;
		MoveAccel.Z = 0.f;

		RecoilState.MovementInertiaVelocity += MoveAccel * DeltaTime;
		RecoilState.MovementInertiaOffset += RecoilState.MovementInertiaVelocity * DeltaTime;

		// Clamp
		const float MaxMOff = Profile->MaxMovementInertiaOffset;
		if (MaxMOff > 0.f)
		{
			RecoilState.MovementInertiaOffset.X = FMath::Clamp(RecoilState.MovementInertiaOffset.X, -MaxMOff, MaxMOff);
			RecoilState.MovementInertiaOffset.Y = FMath::Clamp(RecoilState.MovementInertiaOffset.Y, -MaxMOff, MaxMOff);
			if (FMath::Abs(RecoilState.MovementInertiaOffset.X) >= MaxMOff - KINDA_SMALL_NUMBER)
				RecoilState.MovementInertiaVelocity.X = 0.f;
			if (FMath::Abs(RecoilState.MovementInertiaOffset.Y) >= MaxMOff - KINDA_SMALL_NUMBER)
				RecoilState.MovementInertiaVelocity.Y = 0.f;
		}

		// Snap to zero
		if (RecoilState.MovementInertiaOffset.SizeSquared() < 0.0001f && RecoilState.MovementInertiaVelocity.SizeSquared() < 0.01f)
		{
			RecoilState.MovementInertiaOffset = FVector::ZeroVector;
			RecoilState.MovementInertiaVelocity = FVector::ZeroVector;
		}

		TotalPosOffset += RecoilState.MovementInertiaOffset;
	}

	// ═══════════════════════════════════════════════════════════════
	// 6. FOOTSTEP IMPACT — micro-impulse at each step (bob zero-crossing)
	//    Piggybacks on walk bob phase: each half-cycle = one footstep.
	//    Adds subtle rotation kick to landing spring for realism.
	// ═══════════════════════════════════════════════════════════════
	if (Profile->WalkBobAmplitudeV > 0.f && HorizontalSpeed > 50.f && !bInAir)
	{
		// Detect zero-crossing of sin(phase) → each crossing = footstep
		float SinCurrent = FMath::Sin(RecoilState.WalkBobPhase);
		float SinPrev = FMath::Sin(RecoilState.WalkBobPhase - Profile->WalkBobFrequency * DeltaTime * UE_TWO_PI * FMath::Clamp(HorizontalSpeed / Profile->WalkBobReferenceSpeed, 0.f, 1.f));

		if (SinCurrent * SinPrev < 0.f)  // sign changed → zero crossing
		{
			// Small downward impulse on landing spring
			float StepIntensity = FMath::Clamp(HorizontalSpeed / Profile->WalkBobReferenceSpeed, 0.f, 1.f);
			StepIntensity *= FMath::Lerp(1.f, Profile->ADSBobMultiplier, ADSAlpha);
			float SprintMul = StateBobMult;
			RecoilState.LandingImpactVelocity.Z -= 15.f * StepIntensity * SprintMul;

			// Tiny random rotation kick
			float RollKick = FMath::FRandRange(-0.3f, 0.3f) * StepIntensity * SprintMul;
			TotalRotOffset.Roll += RollKick;
		}
	}

	// Store combined output for UpdateCamera
	RecoilState.MotionPositionOffset = TotalPosOffset;
	RecoilState.MotionRotationOffset = TotalRotOffset;
}
