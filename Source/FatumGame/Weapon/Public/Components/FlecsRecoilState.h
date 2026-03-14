// Game-thread-only weapon recoil state.
// Tracks pattern index, kick spring-damper, and screen shake oscillator.
// Updated each frame in AFlecsCharacter::Tick().

#pragma once

#include "CoreMinimal.h"

class UFlecsWeaponProfile;

struct FWeaponRecoilState
{
	// ── Pattern Recoil (permanent until player compensates) ──
	int32 ShotIndex = 0;
	float PatternResetTimer = 0.f;

	// ── Kick / View Punch (auto-recovering spring-damper) ──
	FVector2D KickVelocity = FVector2D::ZeroVector;  // angular velocity (deg/s)
	FVector2D KickOffset = FVector2D::ZeroVector;     // current displacement (deg)

	// ── Screen Shake (visual-only oscillation) ──
	float ShakeIntensity = 0.f;      // current amplitude (decaying)
	float ShakePhase = 0.f;          // oscillation phase (radians)
	FVector ShakeOffset = FVector::ZeroVector;  // computed this frame (pitch, yaw, roll)

	// ── Raw mouse delta (captured in Look(), consumed by TickWeaponInertia) ──
	FVector2D RawMouseDelta = FVector2D::ZeroVector;  // pure mouse input, no recoil contamination

	// ── Weapon Inertia — rotational (spring-damper lag behind crosshair) ──
	FVector2D InertiaOffset = FVector2D::ZeroVector;    // current offset from crosshair (pitch, yaw degrees)
	FVector2D InertiaVelocity = FVector2D::ZeroVector;  // angular velocity of weapon lag (deg/s)

	// ── Weapon Inertia — positional (mesh shifts on screen, "heavy hands" effect) ──
	FVector InertiaPositionOffset = FVector::ZeroVector;    // local-space displacement (cm)
	FVector InertiaPositionVelocity = FVector::ZeroVector;  // velocity (cm/s)

	// ── Idle Sway (synchronized with crosshair) ──
	float IdleSwayPhase = 0.f;
	FVector2D PrevSwayValue = FVector2D::ZeroVector;  // for delta-based application
	float TimeSinceLastMouseMove = 0.f;  // used when bSwayFadeDuringMouse enabled

	// ── Walk Bob ──
	float WalkBobPhase = 0.f;

	// ── Strafe Tilt ──
	float CurrentStrafeTilt = 0.f;

	// ── Landing Impact (spring-damper) ──
	FVector LandingImpactOffset = FVector::ZeroVector;
	FVector LandingImpactVelocity = FVector::ZeroVector;
	bool bWasInAir = false;
	float TrackedFallSpeed = 0.f;  // max downward speed while airborne (cm/s, positive = falling)

	// ── Sprint Pose ──
	float SprintPoseAlpha = 0.f;

	// ── Movement Inertia (velocity-based spring offset) ──
	FVector MovementInertiaOffset = FVector::ZeroVector;
	FVector MovementInertiaVelocity = FVector::ZeroVector;

	// ── Combined motion output (computed by TickWeaponMotion, applied in UpdateCamera) ──
	FVector MotionPositionOffset = FVector::ZeroVector;  // total positional offset from all motion systems
	FRotator MotionRotationOffset = FRotator::ZeroRotator;  // total rotational offset from all motion systems

	// ── Cached weapon profile (set on equip, read for tuning params) ──
	const UFlecsWeaponProfile* CachedProfile = nullptr;

	void Reset()
	{
		ShotIndex = 0;
		PatternResetTimer = 0.f;
		KickVelocity = FVector2D::ZeroVector;
		KickOffset = FVector2D::ZeroVector;
		ShakeIntensity = 0.f;
		ShakePhase = 0.f;
		ShakeOffset = FVector::ZeroVector;
		RawMouseDelta = FVector2D::ZeroVector;
		InertiaOffset = FVector2D::ZeroVector;
		InertiaVelocity = FVector2D::ZeroVector;
		InertiaPositionOffset = FVector::ZeroVector;
		InertiaPositionVelocity = FVector::ZeroVector;
		IdleSwayPhase = 0.f;
		PrevSwayValue = FVector2D::ZeroVector;
		TimeSinceLastMouseMove = 0.f;
		WalkBobPhase = 0.f;
		CurrentStrafeTilt = 0.f;
		LandingImpactOffset = FVector::ZeroVector;
		LandingImpactVelocity = FVector::ZeroVector;
		bWasInAir = false;
		TrackedFallSpeed = 0.f;
		SprintPoseAlpha = 0.f;
		MovementInertiaOffset = FVector::ZeroVector;
		MovementInertiaVelocity = FVector::ZeroVector;
		MotionPositionOffset = FVector::ZeroVector;
		MotionRotationOffset = FRotator::ZeroRotator;
	}
};
