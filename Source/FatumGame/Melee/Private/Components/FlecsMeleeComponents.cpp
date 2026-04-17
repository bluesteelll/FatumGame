// FlecsMeleeComponents.cpp — FromProfile conversion, reset helpers, and
// direction-buffer / absorb-accumulator logic.

#include "FlecsMeleeComponents.h"
#include "FlecsMeleeProfile.h"

// ═══════════════════════════════════════════════════════════════
// FMeleeWeaponStatic::FromProfile
// ═══════════════════════════════════════════════════════════════

FMeleeWeaponStatic FMeleeWeaponStatic::FromProfile(const UFlecsMeleeProfile* Profile)
{
	check(Profile);

	FMeleeWeaponStatic S;

	// Visuals
	S.EquippedMesh = Profile->EquippedMesh;
	S.AttachOffset = Profile->AttachOffset;

	// Geometry
	S.BladeStartSocket     = Profile->BladeStartSocket;
	S.BladeTipSocket       = Profile->BladeTipSocket;
	S.Reach                = Profile->Reach;
	S.CapsuleRadius        = Profile->CapsuleRadius;
	S.WeaponMass           = Profile->WeaponMass;
	S.SweepSubstepsPerTick = Profile->SweepSubstepsPerTick;

	// Phase timings
	S.WindupTime   = Profile->WindupTime;
	S.ReleaseTime  = Profile->ReleaseTime;
	S.RecoveryTime = Profile->RecoveryTime;

	// Damage
	S.BaseDamage        = Profile->BaseDamage;
	S.BaseImpulse       = Profile->BaseImpulse;
	S.CritChance        = Profile->CritChance;
	S.CritMultiplier    = Profile->CritMultiplier;
	S.DeliveryType      = Profile->DeliveryType;
	S.ReferenceTipSpeed = Profile->ReferenceTipSpeed;
	S.TipSpeedMulMin    = Profile->TipSpeedMulMin;
	S.TipSpeedMulMax    = Profile->TipSpeedMulMax;

	// Penetration — inline copy into FPenetrationStatic slot (§J Q8 MVP choice).
	S.MeleePen.PenetrationBudget         = Profile->PenetrationBudget;
	S.MeleePen.MaxPenetrations           = Profile->MaxPenetrations;
	S.MeleePen.DamageFalloffFactor       = Profile->PenetrationDamageFalloff;
	S.MeleePen.VelocityFalloffFactor     = Profile->PenetrationVelocityFalloff;
	S.MeleePen.RicochetCosAngleThreshold = FMath::Cos(FMath::DegreesToRadians(Profile->PenetrationRicochetAngleDeg));
	S.MeleePen.ImpulseTransferFactor     = Profile->PenetrationImpulseTransfer;

	// Charge
	S.bEnableCharge          = Profile->bEnableCharge;
	S.MinChargeTime          = Profile->MinChargeTime;
	S.MaxChargeTime          = Profile->MaxChargeTime;
	S.bAutoFireAtMaxCharge   = Profile->bAutoFireAtMaxCharge;
	S.bAutoRestartCharge     = Profile->bAutoRestartCharge;
	S.ChargeCurve            = Profile->ChargeCurve;
	S.DamageMaxMultiplier    = Profile->DamageMaxMultiplier;
	S.ImpulseMaxMultiplier   = Profile->ImpulseMaxMultiplier;
	S.PenetrationMaxMultiplier = Profile->PenetrationMaxMultiplier;
	S.SwingSpeedMaxMultiplier  = Profile->SwingSpeedMaxMultiplier;
	S.StaminaCostMaxMultiplier = Profile->StaminaCostMaxMultiplier;

	// Stamina
	S.StaminaCostPerSwing       = Profile->StaminaCostPerSwing;
	S.BaseBlockStaminaCost      = Profile->BaseBlockStaminaCost;
	S.StaminaCostPerBlockSecond = Profile->StaminaCostPerBlockSecond;

	// Block
	S.bCanBlock                      = Profile->bCanBlock;
	S.MinBlockAbsorb                 = Profile->MinBlockAbsorb;
	S.MaxBlockAbsorb                 = Profile->MaxBlockAbsorb;
	S.DirectionalDiscountFactor      = Profile->DirectionalDiscountFactor;
	S.PerfectBlockWindowSeconds      = Profile->PerfectBlockWindowSeconds;
	S.PerfectTimingStaminaMultiplier = Profile->PerfectTimingStaminaMultiplier;

	// Rebound
	S.ReboundRecoveryMultiplier = Profile->ReboundRecoveryMultiplier;

	// VFX
	S.TrailEffect          = Profile->TrailEffect;
	S.ImpactEffectOverride = Profile->ImpactEffectOverride;

	// ─── Validation (blueprint §D) ────────────────────────────────
	checkf(!S.bEnableCharge || S.MinChargeTime < S.MaxChargeTime,
		TEXT("%s: when bEnableCharge, MinChargeTime (%.3f) must be < MaxChargeTime (%.3f)"),
		*Profile->GetName(), S.MinChargeTime, S.MaxChargeTime);

	checkf(!S.bAutoFireAtMaxCharge || S.bEnableCharge,
		TEXT("%s: bAutoFireAtMaxCharge requires bEnableCharge"), *Profile->GetName());

	checkf(S.SweepSubstepsPerTick >= 1 && S.SweepSubstepsPerTick <= 8,
		TEXT("%s: SweepSubstepsPerTick (%d) out of range [1, 8]"),
		*Profile->GetName(), S.SweepSubstepsPerTick);

	checkf(S.MinBlockAbsorb >= 0.f && S.MaxBlockAbsorb <= 1.f && S.MinBlockAbsorb <= S.MaxBlockAbsorb,
		TEXT("%s: block absorb invariant violated (Min=%.3f, Max=%.3f)"),
		*Profile->GetName(), S.MinBlockAbsorb, S.MaxBlockAbsorb);

	checkf(S.PerfectBlockWindowSeconds > 0.f,
		TEXT("%s: PerfectBlockWindowSeconds must be > 0"), *Profile->GetName());

	checkf(S.DirectionalDiscountFactor >= 0.f && S.DirectionalDiscountFactor <= 1.f,
		TEXT("%s: DirectionalDiscountFactor (%.3f) must be in [0, 1]"),
		*Profile->GetName(), S.DirectionalDiscountFactor);

	checkf(!S.bAutoRestartCharge || (S.RecoveryTime + S.MinChargeTime) >= 0.05f,
		TEXT("%s: bAutoRestartCharge with (RecoveryTime + MinChargeTime) < 0.05 would infinite-restart"),
		*Profile->GetName());

	return S;
}

// ═══════════════════════════════════════════════════════════════
// FMeleeWeaponInstance::ResetAllChargeAndSwingState
// ═══════════════════════════════════════════════════════════════

void FMeleeWeaponInstance::ResetAllChargeAndSwingState()
{
	// Intentionally does NOT touch BladeBuffer — owned by equip/on_remove (N-C2, §F.5).

	PendingPayload = FMeleeChargePayload{};
	LatchedPayload = FMeleeChargePayload{};

	bAttackRequested            = false;
	bBlockRequested             = false;
	bWasAttackRequestedLastTick = false;
	bWasBlockRequestedLastTick  = false;

	bIsCharging         = false;
	ChargeAccumulator   = 0.f;
	bPendingAutoRestart = false;

	Phase             = EMeleeAttackPhase::Idle;
	PhaseTimer        = 0.f;
	WindupDuration    = 0.f;
	ReleaseDuration   = 0.f;
	RecoveryDuration  = 0.f;
	ResolvedDirection = EMeleeSwingDirection::Horizontal;
	ShapedT           = 0.f;

	PenState = FSwingPenState{};

	for (int32 i = 0; i < MaxHitsPerSwing; ++i) HitTargetIds[i] = 0;
	HitCount = 0;

	PrevBladeStartWS    = FVector::ZeroVector;
	PrevBladeTipWS      = FVector::ZeroVector;
	LastSweepFrameStamp = 0;
	LastTipSpeed        = 0.f;
	bSwingRebounded     = false;
	bSwingBladeStuck    = false;

	bIsBlocking            = false;
	BlockStartTimestampSim = 0.f;
}

// ═══════════════════════════════════════════════════════════════
// FMeleeAttackDirectionBuffer
// ═══════════════════════════════════════════════════════════════

EMeleeSwingDirection FMeleeAttackDirectionBuffer::Resolve() const
{
	const FVector2f Sum = AccumulatedDelta;
	const float Mag = Sum.Size();

	// Thrust detection: mouse held still (or tiny dead-zone motion) during the whole
	// charge → forward stab. Threshold is in summed yaw/pitch decidegrees — tune
	// via playtest. A quick finger-flick produces 20+ easily, a genuine "hold still"
	// stays well under 3 even on a twitchy hand.
	constexpr float ThrustMagThreshold = 3.0f;
	if (Mag < ThrustMagThreshold)
	{
		return EMeleeSwingDirection::Thrust;
	}

	// Classify by quadrant of (X=yaw, Y=pitch). atan2 returns radians in (-PI, PI].
	//   Horizontal  : mostly yaw motion (left or right)
	//   Vertical    : mostly pitch motion (up or down)
	//   DiagonalTL  : upper-left drag, produces lower-right slash
	//   DiagonalTR  : upper-right drag, produces lower-left slash
	const float Angle = FMath::Atan2(Sum.Y, Sum.X);
	const float Deg   = FMath::RadiansToDegrees(Angle);
	const float AbsD  = FMath::Abs(Deg);

	if (AbsD < 22.5f || AbsD > 157.5f) return EMeleeSwingDirection::Horizontal;
	if (AbsD > 67.5f && AbsD < 112.5f) return EMeleeSwingDirection::Vertical;

	// Diagonals by sign of (yaw, pitch). Convention: Sum.X = yaw delta (right +),
	// Sum.Y = pitch delta (up +). Lower-half drags mirror to the same diagonal
	// categories (symmetric arc).
	const bool bTopHalf = Sum.Y > 0.f;
	const bool bRight   = Sum.X > 0.f;
	return (bTopHalf == bRight) ? EMeleeSwingDirection::DiagonalTR
	                            : EMeleeSwingDirection::DiagonalTL;
}

// ═══════════════════════════════════════════════════════════════
// FPendingBlockAbsorb::AddAbsorb (N-C1 accumulator)
// ═══════════════════════════════════════════════════════════════

void FPendingBlockAbsorb::AddAbsorb(float InStamina, float InAbsorb, EBlockTimingQuality InTiming)
{
	TotalStaminaCost += InStamina;
	MaxAbsorbFraction = FMath::Max(MaxAbsorbFraction, InAbsorb);
	if (InTiming == EBlockTimingQuality::Perfect)
	{
		TimingQuality = EBlockTimingQuality::Perfect;
	}
	++AbsorbCount;
}
