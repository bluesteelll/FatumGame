// FlecsArtillerySubsystem - Character Physics Bridge
// RegisterCharacterBridge, PrepareCharacterStep (mantle/slide FSMs),
// ComputeFreshAlpha, RegisterLocalPlayer.
//
// All character locomotion runs on the sim thread (before StackUp).
// Position readback is in AFlecsCharacter::Tick (game thread, direct Jolt read).

#include "FlecsArtillerySubsystem.h"
#include "FlecsCharacter.h"
#include "FlecsCharacterTypes.h"
#include "FlecsMovementStatic.h"
#include "FBarragePrimitive.h"
#include "BarrageDispatch.h"
#include "FWorldSimOwner.h"
#include "CoordinateUtils.h"
#include "LedgeDetector.h"
#include "PhysicsFilters/FastObjectLayerFilters.h"
#include "EPhysicsLayer.h"
#include "HAL/PlatformTime.h"

// ═══════════════════════════════════════════════════════════════
// CHARACTER PHYSICS BRIDGE
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::RegisterCharacterBridge(AFlecsCharacter* Character)
{
	check(Character);
	check(Character->CachedBarrageBody.IsValid());
	check(Character->InputAtomics.IsValid());

	FCharacterPhysBridge Bridge;
	Bridge.CachedBody = Character->CachedBarrageBody;
	Bridge.InputAtomics = Character->InputAtomics;
	Bridge.CharacterKey = Character->CharacterKey;
	Bridge.SlideActive = Character->SlideActiveAtomic;
	Bridge.MantleActive = Character->MantleActiveAtomic;
	Bridge.Hanging = Character->HangingAtomic;
	Bridge.BlinkAiming = Character->BlinkAimingAtomic;
	Bridge.Teleported = Character->TeleportedAtomic;
	Bridge.MantleType = Character->MantleTypeAtomic;

	// Resolve Flecs entity for this character (bidirectional binding already set)
	Bridge.Entity = GetEntityForBarrageKey(Character->CharacterKey);

	// Cache FBCharacterBase pointer for direct sim-thread access (no per-tick lookup)
	if (CachedBarrageDispatch && CachedBarrageDispatch->JoltGameSim
		&& CachedBarrageDispatch->JoltGameSim->CharacterToJoltMapping)
	{
		TSharedPtr<FBCharacterBase>* CharPtr =
			CachedBarrageDispatch->JoltGameSim->CharacterToJoltMapping->Find(
				Character->CachedBarrageBody->KeyIntoBarrage);
		if (CharPtr && *CharPtr)
		{
			Bridge.CachedFBChar = *CharPtr;
		}
	}
	checkf(Bridge.CachedFBChar, TEXT("RegisterCharacterBridge: Failed to resolve FBCharacter for key %llu"),
		static_cast<uint64>(Character->CharacterKey));

	CharacterBridges.Add(MoveTemp(Bridge));
}

void UFlecsArtillerySubsystem::UnregisterCharacterBridge(FSkeletonKey CharacterKey)
{
	CharacterBridges.RemoveAll([CharacterKey](const FCharacterPhysBridge& B) { return B.CharacterKey == CharacterKey; });
}

// ═══════════════════════════════════════════════════════════════
// FRESH ALPHA COMPUTATION (for AFlecsCharacter::Tick, runs before subsystem Tick)
// ═══════════════════════════════════════════════════════════════

float UFlecsArtillerySubsystem::ComputeFreshAlpha(uint64& OutSimTick) const
{
	OutSimTick = SimWorker.SimTickCount.load(std::memory_order_acquire);
	const float SimDt = SimWorker.LastSimDeltaTime.load(std::memory_order_acquire);
	const double LastSimTime = SimWorker.LastSimTickTimeSeconds.load(std::memory_order_acquire);

	if (SimDt > 0.0f && LastSimTime > 0.0)
	{
		const double TimeSince = FPlatformTime::Seconds() - LastSimTime;
		if (TimeSince >= 0.0)
		{
			return FMath::Clamp(static_cast<float>(TimeSince / SimDt), 0.0f, 1.0f);
		}
	}
	return 1.0f;
}

// ═══════════════════════════════════════════════════════════════
// MANTLE FSM (sim thread) — called by PrepareCharacterStep
// ═══════════════════════════════════════════════════════════════

/** Easing function for mantle phases. */
static float MantleEaseAlpha(uint8 Phase, float LinearAlpha)
{
	if (Phase == 1) // Rise: EaseOut quadratic
		return 1.f - (1.f - LinearAlpha) * (1.f - LinearAlpha);
	if (Phase == 0 || Phase == 2) // GrabTransition or Pull: Smoothstep
		return LinearAlpha * LinearAlpha * (3.f - 2.f * LinearAlpha);
	return LinearAlpha; // Land: linear
}

/** Run mantle FSM for one character. Sets bOutMantling/bOutHanging.
 *  May remove FMantleInstance from Entity. */
static void StepMantleInstance(flecs::entity Entity, FBCharacterBase* FBChar,
                                FMantleInstance* Mantle, const FMovementStatic* MS,
                                float DeltaTime, bool& bOutMantling, bool& bOutHanging)
{
	bOutMantling = true;
	bOutHanging = false;
	Mantle->Timer += DeltaTime;

	if (Mantle->Phase == 4) // Hanging
	{
		bOutHanging = true;
		JPH::Vec3 HangPos(Mantle->EndX, Mantle->EndY, Mantle->EndZ);
		FBChar->mCharacter->SetPosition(HangPos);
		FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
		FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());

		// Check timeout (LedgeGrabMaxDuration=0 means infinite)
		float MaxDur = MS->LedgeGrabMaxDuration;
		if (MaxDur > 0.f && Mantle->Timer > MaxDur)
		{
			Entity.remove<FMantleInstance>();
			bOutMantling = false;
			bOutHanging = false;
		}
		return;
	}

	// GrabTransition, Rise, Pull, Land (Phase 0-3)
	float Alpha = FMath::Clamp(Mantle->Timer / FMath::Max(Mantle->PhaseDuration, 0.001f), 0.f, 1.f);
	float EasedAlpha = MantleEaseAlpha(Mantle->Phase, Alpha);

	JPH::Vec3 Start(Mantle->StartX, Mantle->StartY, Mantle->StartZ);
	JPH::Vec3 End(Mantle->EndX, Mantle->EndY, Mantle->EndZ);
	FBChar->mCharacter->SetPosition(Start + (End - Start) * EasedAlpha);
	FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
	FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());

	// Phase completion
	if (Mantle->Timer < Mantle->PhaseDuration) return;

	Mantle->Timer = 0.f;

	// GrabTransition → Hanging (skip Rise/Pull/Land for initial LedgeGrab)
	if (Mantle->Phase == 0 && Mantle->MantleType == 2)
	{
		Mantle->Phase = 4;
		bOutHanging = true;
		return;
	}

	Mantle->Phase++;

	if (Mantle->Phase == 2) // Rise→Pull
	{
		Mantle->StartX = Mantle->EndX; Mantle->StartY = Mantle->EndY; Mantle->StartZ = Mantle->EndZ;
		Mantle->EndX = Mantle->PullEndX; Mantle->EndY = Mantle->PullEndY; Mantle->EndZ = Mantle->PullEndZ;
		Mantle->PhaseDuration = Mantle->PullDuration;
	}
	else if (Mantle->Phase == 3) // Pull→Land
	{
		Mantle->StartX = Mantle->PullEndX; Mantle->StartY = Mantle->PullEndY; Mantle->StartZ = Mantle->PullEndZ;
		Mantle->EndX   = Mantle->PullEndX; Mantle->EndY   = Mantle->PullEndY; Mantle->EndZ   = Mantle->PullEndZ;
		Mantle->PhaseDuration = Mantle->LandDuration;
	}
	else if (Mantle->Phase == 4) // Land→complete or Hanging
	{
		if (Mantle->MantleType == 2) // LedgeGrab after pull-up → re-hang
		{
			bOutHanging = true;
			Mantle->Timer = 0.f;
		}
		else // Vault/Mantle complete
		{
			Entity.remove<FMantleInstance>();
			bOutMantling = false;
		}
	}
	else if (Mantle->Phase > 4)
	{
		Entity.remove<FMantleInstance>();
		bOutMantling = false;
	}
}

// ═══════════════════════════════════════════════════════════════
// SLIDE STEP (sim thread) — called by PrepareCharacterStep
// ═══════════════════════════════════════════════════════════════

/** Run slide deceleration + steering for one character. Returns true if still sliding.
 *  May remove FSlideInstance from Entity.
 *  If returns true, mLocomotionUpdate is already set (caller should skip normal locomotion). */
static bool StepSlideInstance(flecs::entity Entity, FBCharacterBase* FBChar,
                               FSlideInstance* Slide, const FMovementStatic* MS,
                               float DirX, float DirZ, float DeltaTime)
{
	// Capture slide direction on first tick (from current Jolt velocity)
	if (Slide->SlideDirX == 0.f && Slide->SlideDirZ == 0.f)
	{
		JPH::Vec3 CurVel = FBChar->mCharacter->GetLinearVelocity();
		float HorizLen = FMath::Sqrt(CurVel.GetX() * CurVel.GetX() + CurVel.GetZ() * CurVel.GetZ());
		if (HorizLen > 0.01f)
		{
			Slide->SlideDirX = CurVel.GetX() / HorizLen;
			Slide->SlideDirZ = CurVel.GetZ() / HorizLen;
		}
	}

	Slide->CurrentSpeed -= MS->SlideDeceleration * DeltaTime;
	Slide->CurrentSpeed = FMath::Max(Slide->CurrentSpeed, 0.f);
	Slide->Timer -= DeltaTime;

	// Exit conditions
	bool bOnGround = (FBChar->mCharacter->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);
	if (Slide->Timer <= 0.f || Slide->CurrentSpeed < MS->SlideMinExitSpeed || !bOnGround)
	{
		Entity.remove<FSlideInstance>();
		return false;
	}

	// Direct velocity control (bypasses MoveTowards — slide owns deceleration)
	float SpeedJolt = Slide->CurrentSpeed / 100.f;
	JPH::Vec3 SlideVel(Slide->SlideDirX * SpeedJolt, 0, Slide->SlideDirZ * SpeedJolt);

	// Minor steering from input
	float SlideDirInputLen = FMath::Sqrt(DirX * DirX + DirZ * DirZ);
	if (SlideDirInputLen > 0.01f)
	{
		float SteerRate = MS->SlideMinAcceleration / 100.f;
		float InvDirLen = 1.f / SlideDirInputLen;
		JPH::Vec3 SteerTarget(DirX * InvDirLen * SpeedJolt, 0, DirZ * InvDirLen * SpeedJolt);
		JPH::Vec3 SteerDiff = SteerTarget - SlideVel;
		float SteerLen = SteerDiff.Length();
		float SteerStep = SteerRate * DeltaTime;
		if (SteerLen > SteerStep && SteerLen > 1.0e-6f)
		{
			SlideVel = SlideVel + (SteerDiff / SteerLen) * SteerStep;
		}
	}

	FBChar->mLocomotionUpdate = SlideVel;
	return true;
}

// ═══════════════════════════════════════════════════════════════
// BLINK FSM + DISHONORED-STYLE TARGETING (sim thread)
// ═══════════════════════════════════════════════════════════════

/** Compute blink target using Dishonored-style surface classification.
 *  Returns true if a valid feet position was found. */
static bool ComputeBlinkTarget(const FMovementStatic* MS, UBarrageDispatch* Barrage,
                                FSkeletonKey CharKey, const FCharacterInputAtomics* Input,
                                FVector& OutFeetPos)
{
	// Read camera from atomics
	FVector CamLoc(
		Input->CamLocX.load(std::memory_order_relaxed),
		Input->CamLocY.load(std::memory_order_relaxed),
		Input->CamLocZ.load(std::memory_order_relaxed));
	FVector CamDir(
		Input->CamDirX.load(std::memory_order_relaxed),
		Input->CamDirY.load(std::memory_order_relaxed),
		Input->CamDirZ.load(std::memory_order_relaxed));

	if (CamDir.IsNearlyZero()) return false;
	CamDir.Normalize();

	// Set up filters
	FastIncludeObjectLayerFilter ObjFilter({EPhysicsLayer::NON_MOVING, EPhysicsLayer::MOVING});
	auto BPFilter = Barrage->GetDefaultBroadPhaseLayerFilter(Layers::CAST_QUERY);
	FBarrageKey BodyKey = Barrage->GetBarrageKeyFromSkeletonKey(CharKey);
	auto BodyFilter = Barrage->GetFilterToIgnoreSingleBody(BodyKey);

	double JoltSphereR = MS->BlinkTargetingSphereRadius / 100.0; // cm → Jolt meters

	// 1. SphereCast forward from camera
	TSharedPtr<FHitResult> Hit = MakeShared<FHitResult>();
	Barrage->SphereCast(JoltSphereR, MS->BlinkMaxRange, CamLoc, CamDir,
	                    Hit, BPFilter, ObjFilter, BodyFilter);

	if (!Hit->bBlockingHit)
	{
		// NO HIT → Air blink
		FVector RawTarget = CamLoc + CamDir * MS->BlinkMaxRange;

		// Try floor snap below
		TSharedPtr<FHitResult> FloorHit = MakeShared<FHitResult>();
		Barrage->SphereCast(5.0 / 100.0, MS->BlinkFloorSnapDistance, RawTarget,
		                    -FVector::UpVector, FloorHit, BPFilter, ObjFilter, BodyFilter);

		if (FloorHit->bBlockingHit &&
			FVector::DotProduct(FloorHit->ImpactNormal, FVector::UpVector) > 0.7f)
		{
			OutFeetPos = FloorHit->ImpactPoint;
			return true;
		}

		if (MS->bBlinkAllowAirTarget)
		{
			OutFeetPos = RawTarget;
			return true;
		}
		return false;
	}

	// HIT → Surface classification by ImpactNormal
	float DotUp = FVector::DotProduct(Hit->ImpactNormal, FVector::UpVector);

	if (DotUp > 0.7f)
	{
		// FLOOR — place directly at impact
		OutFeetPos = Hit->ImpactPoint;
		return true;
	}

	if (DotUp < -0.7f)
	{
		// CEILING — pullback along ray + floor snap
		FVector Pullback = Hit->ImpactPoint + Hit->ImpactNormal * (MS->StandingRadius + 10.f);
		TSharedPtr<FHitResult> FloorHit = MakeShared<FHitResult>();
		Barrage->SphereCast(5.0 / 100.0, MS->BlinkFloorSnapDistance, Pullback,
		                    -FVector::UpVector, FloorHit, BPFilter, ObjFilter, BodyFilter);
		if (FloorHit->bBlockingHit &&
			FVector::DotProduct(FloorHit->ImpactNormal, FVector::UpVector) > 0.7f)
		{
			OutFeetPos = FloorHit->ImpactPoint;
			return true;
		}
		return false;
	}

	// WALL — Dishonored ledge-top snap
	FVector WallHit = Hit->ImpactPoint;
	FVector WallNormal = FVector(Hit->ImpactNormal.X, Hit->ImpactNormal.Y, 0.f).GetSafeNormal();
	if (WallNormal.IsNearlyZero()) WallNormal = -CamDir;

	// Probe above wall hit: start above, cast down to find ledge top
	FVector ProbeStart = WallHit - WallNormal * (MS->StandingRadius + 10.f);
	ProbeStart.Z += MS->BlinkLedgeSearchHeight;

	TSharedPtr<FHitResult> LedgeHit = MakeShared<FHitResult>();
	Barrage->SphereCast(5.0 / 100.0, MS->BlinkLedgeSearchHeight, ProbeStart,
	                    -FVector::UpVector, LedgeHit, BPFilter, ObjFilter, BodyFilter);

	if (LedgeHit->bBlockingHit &&
		FVector::DotProduct(LedgeHit->ImpactNormal, FVector::UpVector) > 0.7f)
	{
		FVector LedgeTop = LedgeHit->ImpactPoint;

		// Depth check — ensure ledge has enough depth (CastRay inward)
		FVector DepthOrigin = LedgeTop + FVector(0, 0, 5.f);
		FVector DepthDir = -WallNormal * MS->BlinkMinLedgeDepth;
		TSharedPtr<FHitResult> DepthHit = MakeShared<FHitResult>();
		Barrage->CastRay(DepthOrigin, DepthDir, BPFilter, ObjFilter, BodyFilter, DepthHit);

		if (!DepthHit->bBlockingHit)
		{
			// Place on ledge top, offset from wall by capsule radius
			OutFeetPos = LedgeTop + WallNormal * (MS->StandingRadius + 5.f);
			return true;
		}
	}

	// Fallback: pullback from wall + floor snap
	FVector Pullback = WallHit + WallNormal * (MS->StandingRadius + 10.f);
	TSharedPtr<FHitResult> FloorHit = MakeShared<FHitResult>();
	Barrage->SphereCast(5.0 / 100.0, MS->BlinkFloorSnapDistance, Pullback,
	                    -FVector::UpVector, FloorHit, BPFilter, ObjFilter, BodyFilter);

	if (FloorHit->bBlockingHit &&
		FVector::DotProduct(FloorHit->ImpactNormal, FVector::UpVector) > 0.7f)
	{
		OutFeetPos = FloorHit->ImpactPoint;
		return true;
	}

	return false;
}

/** Step blink FSM for one character. Returns true if teleported this frame.
 *  bOutAiming is set true when in aim mode (game thread uses for time dilation). */
static bool StepBlinkInstance(flecs::entity Entity, FBCharacterBase* FBChar,
                               FBlinkInstance* Blink, FCharacterSimState* SimState,
                               const FMovementStatic* MS, const FCharacterInputAtomics* Input,
                               FCharacterPhysBridge& Bridge, UBarrageDispatch* Barrage,
                               float DeltaTime, bool& bOutAiming)
{
	bOutAiming = false;

	// Lazy init charges
	if (Blink->Charges < 0) Blink->Charges = MS->BlinkMaxCharges;

	// Tick charge recharge
	if (Blink->Charges < MS->BlinkMaxCharges)
	{
		Blink->RechargeTimer += DeltaTime;
		if (Blink->RechargeTimer >= MS->BlinkRechargeTime)
		{
			Blink->RechargeTimer -= MS->BlinkRechargeTime;
			Blink->Charges++;
		}
	}

	// Edge-detect blink button
	bool bHeld = Input->bBlinkHeld.load(std::memory_order_relaxed);
	bool bPressed = bHeld && !SimState->bPrevBlinkHeld;
	bool bReleased = !bHeld && SimState->bPrevBlinkHeld;
	SimState->bPrevBlinkHeld = bHeld;

	bool bTeleported = false;

	switch (Blink->State)
	{
	case 0: // Idle
		if (bPressed && Blink->Charges > 0)
		{
			Blink->State = 1; // HoldCheck
			Blink->HoldTimer = 0.f;
			Blink->bTargetValid = false;
		}
		break;

	case 1: // HoldCheck
		Blink->HoldTimer += DeltaTime;

		if (bReleased)
		{
			// Quick-tap: instant blink
			FVector TargetFeet;
			if (ComputeBlinkTarget(MS, Barrage, Bridge.CharacterKey, Input, TargetFeet))
			{
				// Convert UE cm feet pos to Jolt coords
				JPH::Vec3 JoltPos = CoordinateUtils::ToJoltCoordinates(FVector3d(TargetFeet));
				FBChar->mCharacter->SetPosition(JoltPos);
				FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());
				FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
				Blink->Charges--;
				if (Blink->Charges < MS->BlinkMaxCharges && Blink->RechargeTimer <= 0.f)
					Blink->RechargeTimer = 0.f; // start recharge
				Bridge.Teleported->store(true, std::memory_order_relaxed);
				// Cancel slide if active
				if (Entity.has<FSlideInstance>()) Entity.remove<FSlideInstance>();
				bTeleported = true;
			}
			Blink->State = 0;
			break;
		}

		if (Blink->HoldTimer >= MS->BlinkAimHoldThreshold)
		{
			Blink->State = 2; // Aiming
			bOutAiming = true;
		}
		break;

	case 2: // Aiming
		bOutAiming = true;

		// Update target every tick
		{
			FVector TargetFeet;
			Blink->bTargetValid = ComputeBlinkTarget(MS, Barrage, Bridge.CharacterKey, Input, TargetFeet);
			if (Blink->bTargetValid)
			{
				Blink->TargetX = static_cast<float>(TargetFeet.X);
				Blink->TargetY = static_cast<float>(TargetFeet.Y);
				Blink->TargetZ = static_cast<float>(TargetFeet.Z);
			}
		}

		if (bReleased)
		{
			bOutAiming = false;

			if (Blink->bTargetValid)
			{
				FVector TargetFeet(Blink->TargetX, Blink->TargetY, Blink->TargetZ);
				JPH::Vec3 JoltPos = CoordinateUtils::ToJoltCoordinates(FVector3d(TargetFeet));
				FBChar->mCharacter->SetPosition(JoltPos);
				FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());
				FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
				Blink->Charges--;
				if (Blink->Charges < MS->BlinkMaxCharges && Blink->RechargeTimer <= 0.f)
					Blink->RechargeTimer = 0.f;
				Bridge.Teleported->store(true, std::memory_order_relaxed);
				if (Entity.has<FSlideInstance>()) Entity.remove<FSlideInstance>();
				bTeleported = true;
			}
			Blink->State = 0;
			break;
		}
		break;
	}

	return bTeleported;
}

// ═══════════════════════════════════════════════════════════════
// MANTLE ACTIVATION HELPERS (sim thread)
// ═══════════════════════════════════════════════════════════════

/** Geometry: estimated distance from hands (gripping ledge) to feet. */
static float ComputeHandToFeetDist(float StandingHalfHeight)
{
	return StandingHalfHeight * 2.f - 25.f;
}

/** Try to activate mantle on sim thread. Runs FLedgeDetector, builds FMantleInstance.
 *  Returns true if mantle was created (caller should skip normal jump). */
static bool TryActivateMantle(flecs::entity Entity, FBCharacterBase* FBChar,
                               const FMovementStatic* MS, FCharacterSimState* SimState,
                               const FCharacterInputAtomics* Input,
                               UBarrageDispatch* Barrage, FSkeletonKey CharKey,
                               bool bOnGround)
{
	if (SimState->MantleCooldownTimer > 0.f) return false;

	// Get character feet position from Jolt
	JPH::Vec3 JoltPos = FBChar->mCharacter->GetPosition();
	FVector3d FeetPosD = CoordinateUtils::FromJoltCoordinatesD(JoltPos);
	FVector FeetPos(FeetPosD);

	// Camera look direction for detection
	FVector LookDir(
		Input->CamDirX.load(std::memory_order_relaxed),
		Input->CamDirY.load(std::memory_order_relaxed),
		Input->CamDirZ.load(std::memory_order_relaxed));
	if (LookDir.IsNearlyZero()) return false;
	LookDir.Normalize();

	float MaxReach = bOnGround ? MS->MantleMaxHeight : MS->LedgeGrabMaxHeight;

	FLedgeCandidate Candidate;
	FLedgeDetector::Detect(FeetPos, LookDir,
	                       MS->StandingRadius, MS->StandingHalfHeight,
	                       MaxReach, MS, Barrage, CharKey, Candidate);

	if (!Candidate.bValid) return false;

	// Determine type
	uint8 MType;
	if (!bOnGround)
		MType = 2; // LedgeGrab
	else if (Candidate.LedgeHeight <= MS->MantleVaultMaxHeight)
		MType = 0; // Vault
	else
		MType = 1; // Mantle

	// Compute geometry (UE coords → Jolt coords)
	const FVector& LedgeTop = Candidate.LedgeTopPoint;
	const FVector& WallNormal = Candidate.WallNormal;
	float CapsuleR = MS->CrouchRadius; // capsule will be crouch during mantle

	FVector PullEndPos = FVector(
		LedgeTop.X + WallNormal.X * (CapsuleR + 10.f),
		LedgeTop.Y + WallNormal.Y * (CapsuleR + 10.f),
		LedgeTop.Z);

	JPH::Vec3 JoltStart = CoordinateUtils::ToJoltCoordinates(FVector3d(FeetPos));
	JPH::Vec3 JoltPullEnd = CoordinateUtils::ToJoltCoordinates(FVector3d(PullEndPos));
	JPH::Vec3 JoltWallN = CoordinateUtils::ToJoltUnitVector(FVector3d(WallNormal));

	FMantleInstance MI;
	MI.StartX = JoltStart.GetX(); MI.StartY = JoltStart.GetY(); MI.StartZ = JoltStart.GetZ();
	MI.PullEndX = JoltPullEnd.GetX(); MI.PullEndY = JoltPullEnd.GetY(); MI.PullEndZ = JoltPullEnd.GetZ();
	MI.WallNormalX = JoltWallN.GetX(); MI.WallNormalZ = JoltWallN.GetZ();
	MI.PullDuration = MS->MantlePullDuration;
	MI.LandDuration = MS->MantleLandDuration;
	MI.Timer = 0.f;
	MI.MantleType = MType;
	MI.bCanPullUp = Candidate.bCanPullUp;

	if (MType == 2) // LedgeGrab
	{
		float HandToFeet = ComputeHandToFeetDist(MS->StandingHalfHeight);
		FVector HangFeetPos = FVector(
			LedgeTop.X + WallNormal.X * (CapsuleR + 5.f),
			LedgeTop.Y + WallNormal.Y * (CapsuleR + 5.f),
			LedgeTop.Z - HandToFeet);
		JPH::Vec3 JoltHang = CoordinateUtils::ToJoltCoordinates(FVector3d(HangFeetPos));
		MI.EndX = JoltHang.GetX(); MI.EndY = JoltHang.GetY(); MI.EndZ = JoltHang.GetZ();
		MI.PhaseDuration = MS->LedgeGrabTransitionDuration;
		MI.Phase = 0; // GrabTransition
	}
	else // Vault / Mantle
	{
		FVector RiseEndPos = FeetPos;
		RiseEndPos.Z = LedgeTop.Z;
		JPH::Vec3 JoltRiseEnd = CoordinateUtils::ToJoltCoordinates(FVector3d(RiseEndPos));
		MI.EndX = JoltRiseEnd.GetX(); MI.EndY = JoltRiseEnd.GetY(); MI.EndZ = JoltRiseEnd.GetZ();
		MI.PhaseDuration = MS->MantleRiseDuration;
		MI.Phase = 1; // Rise (skip GrabTransition)
	}

	Entity.set<FMantleInstance>(MI);
	return true;
}

/** Handle mantle hang exit input (pull-up, wall-jump, let-go).
 *  Called when mantling in Phase 4 (Hanging). */
static void HandleHangExit(flecs::entity Entity, FBCharacterBase* FBChar,
                            FMantleInstance* Mantle, const FMovementStatic* MS,
                            const FCharacterInputAtomics* Input,
                            bool bJumpEdge, bool bCrouchEdge)
{
	if (bJumpEdge)
	{
		// Read camera direction from atomics
		FVector CamDir(
			Input->CamDirX.load(std::memory_order_relaxed),
			Input->CamDirY.load(std::memory_order_relaxed),
			Input->CamDirZ.load(std::memory_order_relaxed));
		FVector HorizFwd = FVector(CamDir.X, CamDir.Y, 0.f).GetSafeNormal();
		if (HorizFwd.IsNearlyZero())
		{
			// Fallback: push away from wall
			FVector3f WallN3f = CoordinateUtils::FromJoltUnitVector(
				JPH::Vec3(Mantle->WallNormalX, 0, Mantle->WallNormalZ));
			HorizFwd = FVector(WallN3f);
		}

		// Wall normal in UE coords
		FVector3f WallNUE = CoordinateUtils::FromJoltUnitVector(
			JPH::Vec3(Mantle->WallNormalX, 0, Mantle->WallNormalZ));
		float ForwardDot = FVector::DotProduct(HorizFwd, -FVector(WallNUE));

		if (ForwardDot > 0.3f && Mantle->bCanPullUp)
		{
			// Pull up: Rise from hang to ledge level, then Pull forward onto ledge
			Mantle->StartX = Mantle->EndX;
			Mantle->StartY = Mantle->EndY;
			Mantle->StartZ = Mantle->EndZ;
			// Rise end: same XZ as hang, Y at ledge level (PullEndY in Jolt coords)
			Mantle->EndX = Mantle->EndX;
			Mantle->EndY = Mantle->PullEndY; // Jolt Y = UE Z, at ledge top
			Mantle->EndZ = Mantle->EndZ;
			Mantle->Timer = 0.f;
			Mantle->PhaseDuration = MS->PullUpDuration;
			Mantle->Phase = 1; // Rise
			Mantle->MantleType = 1; // Treat as Mantle (completes instead of re-hanging)
		}
		else if (ForwardDot > 0.3f && !Mantle->bCanPullUp)
		{
			// Ceiling blocked — consume input, no action
		}
		else
		{
			// Wall jump: remove mantle, apply directional force
			float HForce = MS->WallJumpHorizontalForce / 100.f; // cm/s → Jolt m/s
			float VForce = MS->WallJumpVerticalForce / 100.f;
			JPH::Vec3 JoltJumpDir = CoordinateUtils::ToJoltCoordinates(
				FVector3d(HorizFwd * MS->WallJumpHorizontalForce + FVector::UpVector * MS->WallJumpVerticalForce));
			// OtherForce expects cm/s, but Jolt uses m/s. Actually ApplyForce handles this...
			// Use direct velocity set for wall jump (simpler, more predictable)
			JPH::Vec3 JoltVel(
				static_cast<float>(HorizFwd.X) * HForce,
				VForce,
				static_cast<float>(HorizFwd.Y) * HForce);
			// UE Y → Jolt Z, UE Z → Jolt Y
			JPH::Vec3 JoltVelSwapped(
				static_cast<float>(HorizFwd.X) * HForce,
				VForce,
				static_cast<float>(HorizFwd.Y) * HForce);

			Entity.remove<FMantleInstance>();
			FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());

			// Apply wall jump as OtherForce (cm/s, will be converted by Barrage)
			FVector3d ForceUE(HorizFwd * MS->WallJumpHorizontalForce + FVector::UpVector * MS->WallJumpVerticalForce);
			// Use SetLinearVelocity directly in Jolt coords
			JPH::Vec3 JoltForce = CoordinateUtils::ToJoltCoordinates(ForceUE);
			// ToJoltCoordinates divides by 100 (cm→m) and swaps Y↔Z
			FBChar->mCharacter->SetLinearVelocity(JoltForce);
		}
	}
	else if (bCrouchEdge)
	{
		// Let go
		Entity.remove<FMantleInstance>();
	}
}

// ═══════════════════════════════════════════════════════════════
// PREPARE CHARACTER STEP (sim thread, before StackUp)
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::PrepareCharacterStep(float DeltaTime)
{
	for (FCharacterPhysBridge& Bridge : CharacterBridges)
	{
		if (!Bridge.CachedFBChar || !Bridge.InputAtomics) continue;
		FBCharacterBase* FBChar = Bridge.CachedFBChar.Get();
		const FCharacterInputAtomics* Input = Bridge.InputAtomics.Get();

		// ── 1. Read ALL input atomics ──
		float DirX = Input->DirX.load(std::memory_order_relaxed);
		float DirZ = Input->DirZ.load(std::memory_order_relaxed);
		bool bJumpPressed = Input->bJumpPressed.load(std::memory_order_relaxed);
		bool bCrouchHeld = Input->bCrouchHeld.load(std::memory_order_relaxed);
		bool bSprinting = Input->bSprinting.load(std::memory_order_relaxed);

		// Consume one-shot jump press
		if (bJumpPressed) Input->bJumpPressed.store(false, std::memory_order_relaxed);

		// ── 2. Read Flecs components ──
		if (!Bridge.Entity.is_valid())
		{
			FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
			continue;
		}

		const FMovementStatic* MS = Bridge.Entity.try_get<FMovementStatic>();
		const FCharacterMoveState* State = Bridge.Entity.try_get<FCharacterMoveState>();
		FCharacterSimState* SimState = Bridge.Entity.try_get_mut<FCharacterSimState>();
		if (!MS)
		{
			FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
			continue;
		}

		// Edge-detect buttons (using SimState prev values)
		bool bJumpEdge = false, bCrouchEdge = false;
		if (SimState)
		{
			bJumpEdge = bJumpPressed && !SimState->bPrevJumpPressed;
			bCrouchEdge = bCrouchHeld && !SimState->bPrevCrouchHeld;
			SimState->bPrevJumpPressed = bJumpPressed;
			SimState->bPrevCrouchHeld = bCrouchHeld;
		}

		// Ground state
		bool bOnGround = (FBChar->mCharacter->GetGroundState()
			== JPH::CharacterVirtual::EGroundState::OnGround);

		// ── 3. Mantle step ──
		FMantleInstance* Mantle = Bridge.Entity.try_get_mut<FMantleInstance>();
		bool bMantling = false;
		bool bHanging = false;
		if (Mantle)
		{
			// Handle hang exit input BEFORE stepping (so pull-up transitions take effect)
			if (Mantle->Phase == 4 && SimState)
			{
				HandleHangExit(Bridge.Entity, FBChar, Mantle, MS, Input, bJumpEdge, bCrouchEdge);
				// Re-read Mantle — may have been removed by wall-jump/let-go
				Mantle = Bridge.Entity.try_get_mut<FMantleInstance>();
			}

			if (Mantle)
			{
				StepMantleInstance(Bridge.Entity, FBChar, Mantle, MS, DeltaTime, bMantling, bHanging);
			}
		}
		Bridge.MantleActive->store(bMantling, std::memory_order_relaxed);
		Bridge.Hanging->store(bHanging, std::memory_order_relaxed);
		if (bMantling && Mantle)
		{
			Bridge.MantleType->store(Mantle->MantleType, std::memory_order_relaxed);
		}

		if (bMantling)
		{
			Bridge.SlideActive->store(false, std::memory_order_relaxed);
			Bridge.BlinkAiming->store(false, std::memory_order_relaxed);
			goto TickTimers;
		}

		{
			// ── 4. Slide step (with input handling) ──
			FSlideInstance* Slide = Bridge.Entity.try_get_mut<FSlideInstance>();
			bool bSliding = false;
			if (Slide)
			{
				// Jump during slide → slide-cancel jump
				if (bJumpEdge)
				{
					Bridge.Entity.remove<FSlideInstance>();
					// Apply jump force directly (cm/s → Jolt via OtherForce)
					FBarragePrimitive::ApplyForce(
						FVector3d(0, 0, MS->SlideJumpVelocity),
						Bridge.CachedBody, PhysicsInputType::OtherForce);
					bJumpEdge = false; // consumed
				}
				// Crouch released during slide → end slide
				else if (!bCrouchHeld)
				{
					Bridge.Entity.remove<FSlideInstance>();
				}
				else
				{
					bSliding = StepSlideInstance(Bridge.Entity, FBChar, Slide, MS, DirX, DirZ, DeltaTime);
				}
			}
			Bridge.SlideActive->store(bSliding, std::memory_order_relaxed);

			// ── 5. Blink FSM (Tier 2 — coexists with slide) ──
			FBlinkInstance* Blink = Bridge.Entity.try_get_mut<FBlinkInstance>();
			bool bBlinkAiming = false;
			bool bBlinkTeleported = false;
			if (Blink && SimState && CachedBarrageDispatch)
			{
				bBlinkTeleported = StepBlinkInstance(Bridge.Entity, FBChar, Blink, SimState, MS,
				                                      Input, Bridge, CachedBarrageDispatch,
				                                      DeltaTime, bBlinkAiming);
				if (bBlinkTeleported)
				{
					bSliding = false;
					Bridge.SlideActive->store(false, std::memory_order_relaxed);
				}
			}
			Bridge.BlinkAiming->store(bBlinkAiming, std::memory_order_relaxed);

			if (bSliding || bBlinkTeleported) goto TickTimers;

			// ── 6. No movement ability active — check activations ──

			// 6a. Jump → try mantle detection → if found → create FMantleInstance
			if (bJumpEdge)
			{
				bool bMantleCreated = false;
				if (SimState && CachedBarrageDispatch)
				{
					bMantleCreated = TryActivateMantle(Bridge.Entity, FBChar, MS, SimState,
					                                    Input, CachedBarrageDispatch,
					                                    Bridge.CharacterKey, bOnGround);
				}

				if (bMantleCreated)
				{
					Bridge.MantleActive->store(true, std::memory_order_relaxed);
					const FMantleInstance* NewMantle = Bridge.Entity.try_get<FMantleInstance>();
					if (NewMantle) Bridge.MantleType->store(NewMantle->MantleType, std::memory_order_relaxed);
					goto TickTimers;
				}

				// 6b. No mantle → normal jump (with coyote time)
				if (SimState && (bOnGround || SimState->CoyoteTimer > 0.f))
				{
					float JumpVel = (State && State->Posture == 1) ? MS->CrouchJumpVelocity : MS->JumpVelocity;
					FBarragePrimitive::ApplyForce(
						FVector3d(0, 0, JumpVel),
						Bridge.CachedBody, PhysicsInputType::OtherForce);
					SimState->CoyoteTimer = 0.f;
					SimState->JumpBufferTimer = 0.f;
				}
				else if (SimState)
				{
					// Airborne, no coyote → buffer jump
					SimState->JumpBufferTimer = MS->JumpBufferSeconds;
				}
			}

			// 6c. Crouch press + sprint + ground + speed → slide activation
			if (bCrouchEdge && bSprinting && bOnGround)
			{
				JPH::Vec3 CurVel = FBChar->mCharacter->GetLinearVelocity();
				float HorizSpeedCm = FMath::Sqrt(CurVel.GetX() * CurVel.GetX() + CurVel.GetZ() * CurVel.GetZ()) * 100.f;
				if (HorizSpeedCm >= MS->SlideMinEntrySpeed)
				{
					FSlideInstance SI;
					SI.CurrentSpeed = HorizSpeedCm + MS->SlideInitialSpeedBoost;
					SI.Timer = MS->SlideMaxDuration;
					Bridge.Entity.set<FSlideInstance>(SI);
					Bridge.SlideActive->store(true, std::memory_order_relaxed);
					goto TickTimers;
				}
			}

			// ── 7. Normal locomotion ──
			float TargetSpeedCm;
			float AccelCm;
			if (bSprinting && (!State || State->Posture == 0))
			{
				TargetSpeedCm = MS->SprintSpeed;
				AccelCm = MS->SprintAcceleration;
			}
			else
			{
				TargetSpeedCm = MS->WalkSpeed;
				AccelCm = MS->GroundAcceleration;
				if (State)
				{
					switch (State->Posture)
					{
					case 1: TargetSpeedCm = MS->CrouchSpeed; break;
					case 2: TargetSpeedCm = MS->ProneSpeed; break;
					}
				}
			}

			float DecelCm = MS->GroundDeceleration;
			float AirAccelCm = MS->AirAcceleration;

			float DirLen = FMath::Sqrt(DirX * DirX + DirZ * DirZ);
			JPH::Vec3 TargetH = JPH::Vec3::sZero();
			if (DirLen > 0.01f)
			{
				float InvDirLen = 1.f / DirLen;
				float SpeedJolt = TargetSpeedCm / 100.f;
				TargetH = JPH::Vec3(DirX * InvDirLen * SpeedJolt, 0, DirZ * InvDirLen * SpeedJolt);
			}

			JPH::Vec3 CurVelo = FBChar->mCharacter->GetLinearVelocity();
			JPH::Vec3 CurH(CurVelo.GetX(), 0, CurVelo.GetZ());

			float AccelRate;
			if (bOnGround)
				AccelRate = TargetH.IsNearZero() ? (DecelCm / 100.f) : (AccelCm / 100.f);
			else
				AccelRate = AirAccelCm / 100.f;

			JPH::Vec3 Diff = TargetH - CurH;
			float DiffLen = Diff.Length();
			float MoveStep = AccelRate * DeltaTime;
			JPH::Vec3 SmoothedH;
			if (DiffLen <= MoveStep || DiffLen < 1.0e-6f)
				SmoothedH = TargetH;
			else
				SmoothedH = CurH + (Diff / DiffLen) * MoveStep;

			FBChar->mLocomotionUpdate = SmoothedH;
		}

	TickTimers:
		// ── 8. Tick sim-thread timers ──
		if (SimState)
		{
			// Coyote time: start when leaving ground (not from jump)
			if (SimState->bWasGrounded && !bOnGround)
			{
				SimState->CoyoteTimer = MS->CoyoteTimeSeconds;
			}
			if (SimState->CoyoteTimer > 0.f) SimState->CoyoteTimer -= DeltaTime;

			// Jump buffer: consume on landing (must check BEFORE updating bWasGrounded)
			if (!SimState->bWasGrounded && bOnGround && SimState->JumpBufferTimer > 0.f)
			{
				// Buffered jump on landing
				float JumpVel = (State && State->Posture == 1) ? MS->CrouchJumpVelocity : MS->JumpVelocity;
				FBarragePrimitive::ApplyForce(
					FVector3d(0, 0, JumpVel),
					Bridge.CachedBody, PhysicsInputType::OtherForce);
				SimState->JumpBufferTimer = 0.f;
			}
			if (SimState->JumpBufferTimer > 0.f) SimState->JumpBufferTimer -= DeltaTime;

			SimState->bWasGrounded = bOnGround;

			// Mantle cooldown
			if (SimState->MantleCooldownTimer > 0.f)
			{
				SimState->MantleCooldownTimer -= DeltaTime;
				if (SimState->MantleCooldownTimer < 0.f) SimState->MantleCooldownTimer = 0.f;
			}

			// Set mantle cooldown when mantle just ended (transition from mantling to not-mantling)
			// This is handled by MantleInstance removal — cooldown set in StepMantleInstance exit

			// 10Hz airborne ledge detection (for auto-ledge-grab)
			if (!bOnGround && !Bridge.Entity.has<FMantleInstance>() && CachedBarrageDispatch
				&& SimState->MantleCooldownTimer <= 0.f)
			{
				SimState->AirDetectionTimer -= DeltaTime;
				if (SimState->AirDetectionTimer <= 0.f)
				{
					SimState->AirDetectionTimer = 0.1f; // 10Hz

					JPH::Vec3 JoltPos = FBChar->mCharacter->GetPosition();
					FVector3d FeetPosD = CoordinateUtils::FromJoltCoordinatesD(JoltPos);
					FVector FeetPos(FeetPosD);
					FVector LookDir(
						Input->CamDirX.load(std::memory_order_relaxed),
						Input->CamDirY.load(std::memory_order_relaxed),
						Input->CamDirZ.load(std::memory_order_relaxed));
					if (!LookDir.IsNearlyZero())
					{
						LookDir.Normalize();
						FLedgeCandidate Candidate;
						FLedgeDetector::Detect(FeetPos, LookDir,
						                       MS->StandingRadius, MS->StandingHalfHeight,
						                       MS->LedgeGrabMaxHeight, MS, CachedBarrageDispatch,
						                       Bridge.CharacterKey, Candidate);

						// Auto-grab if valid and player is pressing jump
						// (or if falling past a ledge — could be auto-detect)
						// For now: just cache. The jump input will use it next frame.
						// Actually: if we detect a ledge while airborne, auto-activate
						// (Dishonored auto-grabs ledges when falling near them)
						// Let's keep it manual: only on jump press
					}
				}
			}
			else
			{
				SimState->AirDetectionTimer = 0.f;
			}
		}
	}
}

// ═══════════════════════════════════════════════════════════════
// LOCAL PLAYER REGISTRATION
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::RegisterLocalPlayer(AActor* Player, FSkeletonKey Key)
{
	CachedLocalPlayerActor = Player;
	CachedLocalPlayerKey = Key;
}

void UFlecsArtillerySubsystem::UnregisterLocalPlayer()
{
	CachedLocalPlayerActor = nullptr;
	CachedLocalPlayerKey = FSkeletonKey();
}
