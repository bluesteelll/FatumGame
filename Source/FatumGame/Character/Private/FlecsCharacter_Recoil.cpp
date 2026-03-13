// FlecsCharacter - Recoil processing (game thread only).
// DrainShotEventsAndApplyRecoil, TickKickRecovery, TickScreenShake.

#include "FlecsCharacter.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsRecoilTypes.h"
#include "FlecsRecoilState.h"
#include "FlecsWeaponProfile.h"
#include "Curves/CurveVector.h"

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
		if (Profile->RecoilPatternCurve)
		{
			// Use sim-thread shot counter as ground truth for pattern indexing
			float PatTime = static_cast<float>(Event.ShotIndex);

			// Handle looping: if past curve end, wrap from PatternLoopStartIndex
			float MinTime, MaxTime;
			Profile->RecoilPatternCurve->GetTimeRange(MinTime, MaxTime);

			if (PatTime > MaxTime && MaxTime > MinTime)
			{
				if (Profile->PatternLoopStartIndex >= 0 && static_cast<float>(Profile->PatternLoopStartIndex) < MaxTime)
				{
					float LoopStart = static_cast<float>(Profile->PatternLoopStartIndex);
					float LoopRange = MaxTime - LoopStart;
					PatTime = LoopStart + FMath::Fmod(PatTime - LoopStart, LoopRange);
				}
				// else: clamp — UCurveVector does this automatically
			}

			FVector CurveDelta = Profile->RecoilPatternCurve->GetVectorValue(PatTime);
			FVector2D PatternDelta(CurveDelta.X * Profile->PatternScale, CurveDelta.Y * Profile->PatternScale);

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
			FVector2D SnapDelta(-RecoilState.KickOffset.X, -RecoilState.KickOffset.Y);
			AddControllerPitchInput(SnapDelta.X);
			AddControllerYawInput(SnapDelta.Y);

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
		RecoilState.ShakeOffset = FVector::ZeroVector;
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
	// Use different frequency multipliers for pitch/yaw/roll to avoid correlation
	RecoilState.ShakeOffset.X = Amp * FMath::Sin(RecoilState.ShakePhase);
	RecoilState.ShakeOffset.Y = Amp * FMath::Sin(RecoilState.ShakePhase * 1.37f);
	// Roll: separate amplitude (mechanical rattle), phase offset for decorrelation
	const float RollRatio = Profile->ShakeRollAmplitude / FMath::Max(Profile->ShakeAmplitude, 0.001f);
	RecoilState.ShakeOffset.Z = Amp * RollRatio * FMath::Sin(RecoilState.ShakePhase * 0.83f + 1.7f);
}

// ═══════════════════════════════════════════════════════════════════════════
// TICK WEAPON INERTIA (spring-damper with overshoot)
// Weapon lags behind crosshair when mouse moves. Overshoots when stopping.
// Bullets fire in the lagged direction (InertiaOffset applied in WriteAimDirection).
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::TickWeaponInertia(float DeltaTime, const FVector2D& AimDelta)
{
	const UFlecsWeaponProfile* Profile = RecoilState.CachedProfile;
	if (!Profile || Profile->InertiaStiffness <= 0.f) return;

	// ── Mouse-driven inertia ──
	// AimDelta = how much crosshair moved this frame (mouse only, recoil excluded).
	// Weapon didn't follow yet → offset grows by the delta.
	RecoilState.InertiaOffset -= AimDelta;

	// Track mouse activity for sway fade (when bSwayFadeDuringMouse enabled)
	if (AimDelta.SizeSquared() > 0.01f)
	{
		RecoilState.TimeSinceLastMouseMove = 0.f;
	}
	else
	{
		RecoilState.TimeSinceLastMouseMove += DeltaTime;
	}

	// ── Spring-damper: pull offset toward zero ──
	// Critical damping coefficient = 2 * sqrt(k), then scaled by damping ratio.
	const float k = Profile->InertiaStiffness;
	const float c = 2.f * Profile->InertiaDamping * FMath::Sqrt(k);  // damping coefficient

	FVector2D Accel;
	Accel.X = -k * RecoilState.InertiaOffset.X - c * RecoilState.InertiaVelocity.X;
	Accel.Y = -k * RecoilState.InertiaOffset.Y - c * RecoilState.InertiaVelocity.Y;

	RecoilState.InertiaVelocity += Accel * DeltaTime;
	RecoilState.InertiaOffset += RecoilState.InertiaVelocity * DeltaTime;

	// Clamp max offset (hard limit for extreme flicks)
	const float MaxOff = Profile->MaxInertiaOffset;
	if (MaxOff > 0.f)
	{
		RecoilState.InertiaOffset.X = FMath::Clamp(RecoilState.InertiaOffset.X, -MaxOff, MaxOff);
		RecoilState.InertiaOffset.Y = FMath::Clamp(RecoilState.InertiaOffset.Y, -MaxOff, MaxOff);
		// Kill velocity component pushing past clamp (prevents stored energy → snap-back)
		if (FMath::Abs(RecoilState.InertiaOffset.X) >= MaxOff - KINDA_SMALL_NUMBER)
			RecoilState.InertiaVelocity.X = 0.f;
		if (FMath::Abs(RecoilState.InertiaOffset.Y) >= MaxOff - KINDA_SMALL_NUMBER)
			RecoilState.InertiaVelocity.Y = 0.f;
	}

	// Snap to zero when nearly settled (avoid micro-oscillation)
	if (RecoilState.InertiaOffset.SizeSquared() < 0.0001f && RecoilState.InertiaVelocity.SizeSquared() < 0.01f)
	{
		RecoilState.InertiaOffset = FVector2D::ZeroVector;
		RecoilState.InertiaVelocity = FVector2D::ZeroVector;
	}

	// ── Idle Sway (synchronized with crosshair via AddControllerInput) ──
	if (Profile->IdleSwayAmplitude > 0.f)
	{
		// Optional fade: sway fades out during mouse movement, fades in after 0.5s idle
		float SwayAlpha = 1.f;
		if (Profile->bSwayFadeDuringMouse)
		{
			constexpr float SwayFadeInDelay = 0.5f;
			constexpr float SwayFadeSpeed = 3.f;
			SwayAlpha = FMath::Clamp((RecoilState.TimeSinceLastMouseMove - SwayFadeInDelay) * SwayFadeSpeed, 0.f, 1.f);
		}

		if (SwayAlpha > 0.001f)
		{
			RecoilState.IdleSwayPhase += Profile->IdleSwayFrequency * DeltaTime * UE_TWO_PI;
			if (RecoilState.IdleSwayPhase > UE_TWO_PI * 100.f)
				RecoilState.IdleSwayPhase = FMath::Fmod(RecoilState.IdleSwayPhase, UE_TWO_PI);

			const float Amp = Profile->IdleSwayAmplitude * SwayAlpha;
			FVector2D NewSway;
			NewSway.X = Amp * FMath::Sin(RecoilState.IdleSwayPhase);
			NewSway.Y = Amp * FMath::Sin(RecoilState.IdleSwayPhase * 1.3f + 0.7f);

			// Apply delta (not absolute) to control rotation — moves crosshair AND weapon together
			FVector2D SwayDelta = NewSway - RecoilState.PrevSwayValue;
			AddControllerPitchInput(SwayDelta.X);
			AddControllerYawInput(SwayDelta.Y);
			RecoilState.PrevSwayValue = NewSway;
		}
		else
		{
			// Fade out: remove remaining sway offset
			if (!RecoilState.PrevSwayValue.IsNearlyZero(0.001f))
			{
				FVector2D FadeoutDelta(-RecoilState.PrevSwayValue.X, -RecoilState.PrevSwayValue.Y);
				AddControllerPitchInput(FadeoutDelta.X);
				AddControllerYawInput(FadeoutDelta.Y);
				RecoilState.PrevSwayValue = FVector2D::ZeroVector;
			}
		}
	}
}
