// FlecsCharacter - Recoil processing (game thread only).
// DrainShotEventsAndApplyRecoil, TickKickRecovery, TickScreenShake.

#include "FlecsCharacter.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsRecoilTypes.h"
#include "FlecsRecoilState.h"
#include "FlecsWeaponProfile.h"

// ═══════════════════════════════════════════════════════════════════════════
// DRAIN SHOT EVENTS & APPLY RECOIL IMPULSES
// Called once per Tick, before camera update.
// Drains MPSC queue of FShotFiredEvent from sim thread.
// For each event: applies pattern recoil + kick impulse + shake impulse.
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::DrainShotEventsAndApplyRecoil()
{
	if (!RecoilState.CachedProfile) return;

	UFlecsArtillerySubsystem* Sub = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!Sub) return;

	const UFlecsWeaponProfile* Profile = RecoilState.CachedProfile;

	FShotFiredEvent Event;
	while (Sub->GetPendingShotEvents().Dequeue(Event))
	{
		// Only process events from our weapon
		if (Event.WeaponEntityId != TestWeaponEntityId) continue;

		// ── Pattern Recoil (permanent control rotation delta) ──
		if (Profile->RecoilPattern.Num() > 0)
		{
			// Use sim-thread shot counter as ground truth for pattern indexing
			int32 PatIdx = Event.ShotIndex;
			const int32 PatternLen = Profile->RecoilPattern.Num();

			if (PatIdx >= PatternLen)
			{
				// Past end: loop from PatternLoopStartIndex or clamp to last
				if (Profile->PatternLoopStartIndex >= 0 && Profile->PatternLoopStartIndex < PatternLen)
				{
					int32 LoopRange = PatternLen - Profile->PatternLoopStartIndex;
					PatIdx = Profile->PatternLoopStartIndex + ((PatIdx - Profile->PatternLoopStartIndex) % LoopRange);
				}
				else
				{
					PatIdx = PatternLen - 1;
				}
			}

			FVector2D PatternDelta = Profile->RecoilPattern[PatIdx] * Profile->PatternScale;

			// Add random perturbation
			PatternDelta.X += FMath::FRandRange(-Profile->PatternRandomPitch, Profile->PatternRandomPitch);
			PatternDelta.Y += FMath::FRandRange(-Profile->PatternRandomYaw, Profile->PatternRandomYaw);

			// Apply as permanent control rotation change
			AddControllerPitchInput(PatternDelta.X);
			AddControllerYawInput(PatternDelta.Y);
		}
		RecoilState.ShotIndex = Event.ShotIndex + 1;
		RecoilState.PatternResetTimer = 0.f;

		// ── Kick Impulse (instant offset, spring-damper will recover) ──
		float KickPitch = FMath::FRandRange(Profile->KickPitchMin, Profile->KickPitchMax);
		float KickYaw = FMath::FRandRange(Profile->KickYawMin, Profile->KickYawMax);
		RecoilState.KickOffset += FVector2D(KickPitch, KickYaw);
		// Apply impulse to controller NOW — recovery will undo it
		AddControllerPitchInput(KickPitch);
		AddControllerYawInput(KickYaw);

		// ── Screen Shake Impulse (additive intensity) ──
		RecoilState.ShakeIntensity += Profile->ShakeAmplitude;
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// TICK KICK RECOVERY (spring-damper)
// Pulls KickOffset back toward zero. Applied as control rotation deltas.
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::TickKickRecovery(float DeltaTime)
{
	if (RecoilState.KickOffset.IsNearlyZero(0.001f) && RecoilState.KickVelocity.IsNearlyZero(0.01f))
	{
		// Snap to zero if nearly settled
		if (!RecoilState.KickOffset.IsZero())
		{
			AddControllerPitchInput(-RecoilState.KickOffset.X);
			AddControllerYawInput(-RecoilState.KickOffset.Y);
			RecoilState.KickOffset = FVector2D::ZeroVector;
			RecoilState.KickVelocity = FVector2D::ZeroVector;
		}
		return;
	}

	const UFlecsWeaponProfile* Profile = RecoilState.CachedProfile;
	if (!Profile) return;

	const float w = Profile->KickRecoverySpeed;
	const float d = Profile->KickDamping;

	// Damped harmonic oscillator: a = -w^2 * x - 2*d*w * v
	FVector2D Accel;
	Accel.X = -(w * w) * RecoilState.KickOffset.X - 2.f * d * w * RecoilState.KickVelocity.X;
	Accel.Y = -(w * w) * RecoilState.KickOffset.Y - 2.f * d * w * RecoilState.KickVelocity.Y;

	RecoilState.KickVelocity += Accel * DeltaTime;
	FVector2D OffsetDelta = RecoilState.KickVelocity * DeltaTime;
	RecoilState.KickOffset += OffsetDelta;

	// Apply recovery movement to control rotation
	AddControllerPitchInput(OffsetDelta.X);
	AddControllerYawInput(OffsetDelta.Y);
}

// ═══════════════════════════════════════════════════════════════════════════
// TICK SCREEN SHAKE (visual-only oscillation)
// Computes ShakeOffset for AddLocalRotation in UpdateCamera().
// Does NOT touch control rotation — purely visual.
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::TickScreenShake(float DeltaTime)
{
	if (RecoilState.ShakeIntensity < 0.001f)
	{
		RecoilState.ShakeOffset = FVector2D::ZeroVector;
		return;
	}

	const UFlecsWeaponProfile* Profile = RecoilState.CachedProfile;
	if (!Profile) return;

	// Exponential decay
	RecoilState.ShakeIntensity *= FMath::Exp(-Profile->ShakeDecaySpeed * DeltaTime);

	// Oscillation phase
	RecoilState.ShakePhase += Profile->ShakeFrequency * DeltaTime * UE_TWO_PI;
	// Wrap to prevent float precision issues in long sessions
	if (RecoilState.ShakePhase > UE_TWO_PI * 100.f)
	{
		RecoilState.ShakePhase = FMath::Fmod(RecoilState.ShakePhase, UE_TWO_PI);
	}

	const float Amp = RecoilState.ShakeIntensity;
	// Use different frequency multipliers for pitch/yaw to avoid 1:1 correlation
	RecoilState.ShakeOffset.X = Amp * FMath::Sin(RecoilState.ShakePhase);
	RecoilState.ShakeOffset.Y = Amp * FMath::Sin(RecoilState.ShakePhase * 1.37f);
}
