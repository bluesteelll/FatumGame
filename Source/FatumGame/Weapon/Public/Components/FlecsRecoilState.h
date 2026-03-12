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
	FVector2D ShakeOffset = FVector2D::ZeroVector;  // computed this frame (pitch, yaw)

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
		ShakeOffset = FVector2D::ZeroVector;
	}
};
