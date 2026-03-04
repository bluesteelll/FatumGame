// Ability tick function implementations.
// TickSlide: 1:1 migration from StepSlideInstance.
// TickBlink: 1:1 migration from StepBlinkInstance + ComputeBlinkTarget.
// TickMantle: 1:1 migration from StepMantleInstance + HandleHangExit.

#include "AbilityTickFunctions.h"
#include "ConeImpulse.h"
#include "FlecsAbilityStates.h"
#include "FlecsAbilityDefinition.h" // FKineticBlastConfig
#include "FlecsMovementStatic.h"
#include "FlecsCharacterTypes.h"
#include "FlecsArtillerySubsystem.h"
#include "FBarragePrimitive.h"
#include "FWorldSimOwner.h"
#include "BarrageDispatch.h"
#include "CoordinateUtils.h"
#include "PhysicsFilters/FastObjectLayerFilters.h"
#include "EPhysicsLayer.h"
#include "FlecsStaticComponents.h"    // FDestructibleStatic
#include "FlecsDestructibleProfile.h" // UFlecsDestructibleProfile (full type)
#include "FlecsInstanceComponents.h"  // FFragmentationData
#include "FlecsGameTags.h"            // FTagCollisionFragmentation
#include "FlecsBarrageComponents.h"   // FCollisionPair

// ═══════════════════════════════════════════════════════════════
// DISPATCH TABLE
// ═══════════════════════════════════════════════════════════════

FAbilityTickFn GAbilityTickFunctions[static_cast<int32>(EAbilityTypeId::MAX)];

void InitAbilityTickFunctions()
{
	FMemory::Memzero(GAbilityTickFunctions);
	GAbilityTickFunctions[static_cast<int32>(EAbilityTypeId::Slide)] = &TickSlide;
	GAbilityTickFunctions[static_cast<int32>(EAbilityTypeId::Blink)] = &TickBlink;
	GAbilityTickFunctions[static_cast<int32>(EAbilityTypeId::Mantle)] = &TickMantle;
	GAbilityTickFunctions[static_cast<int32>(EAbilityTypeId::KineticBlast)] = &TickKineticBlast;
}

// ═══════════════════════════════════════════════════════════════
// SHARED HELPERS
// ═══════════════════════════════════════════════════════════════

void CancelSlideAbility(flecs::entity Entity)
{
	FAbilitySystem* AbilSys = Entity.try_get_mut<FAbilitySystem>();
	if (!AbilSys) return;
	int32 Idx = AbilSys->FindSlotByType(EAbilityTypeId::Slide);
	if (Idx != INDEX_NONE && AbilSys->IsSlotActive(Idx))
	{
		AbilSys->DeactivateSlot(Idx);
		FSlideState* S = Entity.try_get_mut<FSlideState>();
		if (S) S->Reset();
	}
}

// ═══════════════════════════════════════════════════════════════
// TICK SLIDE
// ═══════════════════════════════════════════════════════════════

EAbilityTickResult TickSlide(FAbilityTickContext& Ctx, FAbilitySlot& Slot)
{
	FSlideState* Slide = Ctx.Entity.try_get_mut<FSlideState>();
	checkf(Slide, TEXT("TickSlide: FSlideState missing on entity"));

	const FMovementStatic* MS = Ctx.MovementStatic;

	// Jump during slide → slide-cancel jump
	if (Ctx.bJumpPressed)
	{
		FBarragePrimitive::ApplyForce(
			FVector3d(0, 0, MS->SlideJumpVelocity * Ctx.VelocityScale),
			Ctx.Bridge->CachedBody, PhysicsInputType::OtherForce);
		Slide->Reset();
		return EAbilityTickResult::EndAndConsume;
	}

	// Crouch released during slide → end slide
	if (!Ctx.bCrouchHeld)
	{
		Slide->Reset();
		return EAbilityTickResult::End;
	}

	// Capture slide direction on first tick (from current Jolt velocity)
	if (Slide->SlideDirX == 0.f && Slide->SlideDirZ == 0.f)
	{
		JPH::Vec3 CurVel = Ctx.FBChar->mCharacter->GetLinearVelocity();
		float HorizLen = FMath::Sqrt(CurVel.GetX() * CurVel.GetX() + CurVel.GetZ() * CurVel.GetZ());
		if (HorizLen > 0.01f)
		{
			Slide->SlideDirX = CurVel.GetX() / HorizLen;
			Slide->SlideDirZ = CurVel.GetZ() / HorizLen;
		}
	}

	// Deceleration
	Slide->CurrentSpeed -= MS->SlideDeceleration * Ctx.DeltaTime;
	Slide->CurrentSpeed = FMath::Max(Slide->CurrentSpeed, 0.f);
	Slide->Timer -= Ctx.DeltaTime;

	// Exit conditions: timer expired, speed too low, left ground
	if (Slide->Timer <= 0.f || Slide->CurrentSpeed < MS->SlideMinExitSpeed || !Ctx.bOnGround)
	{
		Slide->Reset();
		return EAbilityTickResult::End;
	}

	// Direct velocity control (bypasses MoveTowards — slide owns deceleration)
	float SpeedJolt = Slide->CurrentSpeed / 100.f;
	JPH::Vec3 SlideVel(Slide->SlideDirX * SpeedJolt, 0, Slide->SlideDirZ * SpeedJolt);

	// Minor steering from input
	float SlideDirInputLen = FMath::Sqrt(Ctx.DirX * Ctx.DirX + Ctx.DirZ * Ctx.DirZ);
	if (SlideDirInputLen > 0.01f)
	{
		float SteerRate = MS->SlideMinAcceleration / 100.f;
		float InvDirLen = 1.f / SlideDirInputLen;
		JPH::Vec3 SteerTarget(Ctx.DirX * InvDirLen * SpeedJolt, 0, Ctx.DirZ * InvDirLen * SpeedJolt);
		JPH::Vec3 SteerDiff = SteerTarget - SlideVel;
		float SteerLen = SteerDiff.Length();
		float SteerStep = SteerRate * Ctx.DeltaTime;
		if (SteerLen > SteerStep && SteerLen > 1.0e-6f)
		{
			SlideVel = SlideVel + (SteerDiff / SteerLen) * SteerStep;
		}
	}

	Ctx.FBChar->mLocomotionUpdate = SlideVel * Ctx.VelocityScale;
	return EAbilityTickResult::Continue;
}

// ═══════════════════════════════════════════════════════════════
// BLINK: DISHONORED-STYLE TARGETING
// ═══════════════════════════════════════════════════════════════

/** Compute blink target using Dishonored-style surface classification.
 *  Returns true if a valid feet position was found. */
static bool ComputeBlinkTarget(const FMovementStatic* MS, UBarrageDispatch* Barrage,
                                FSkeletonKey CharKey, const FCharacterInputAtomics* Input,
                                FVector& OutFeetPos)
{
	// Read camera from atomics
	FVector CamLoc(
		Input->CamLocX.Read(),
		Input->CamLocY.Read(),
		Input->CamLocZ.Read());
	FVector CamDir(
		Input->CamDirX.Read(),
		Input->CamDirY.Read(),
		Input->CamDirZ.Read());

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

// ═══════════════════════════════════════════════════════════════
// TICK BLINK (1:1 port of StepBlinkInstance)
// ═══════════════════════════════════════════════════════════════

EAbilityTickResult TickBlink(FAbilityTickContext& Ctx, FAbilitySlot& Slot)
{
	FBlinkState* Blink = Ctx.Entity.try_get_mut<FBlinkState>();
	checkf(Blink, TEXT("TickBlink: FBlinkState missing on entity"));

	const FMovementStatic* MS = Ctx.MovementStatic;

	// Clear per-frame flag (set below if teleport happens)
	Blink->bTeleportedThisFrame = false;

	// Edge-detect blink button (using FBlinkState.bPrevBlinkHeld)
	bool bHeld = Ctx.Input->BlinkHeld.Read();
	bool bPressed = bHeld && !Blink->bPrevBlinkHeld;
	bool bReleased = !bHeld && Blink->bPrevBlinkHeld;
	Blink->bPrevBlinkHeld = bHeld;

	switch (Blink->State)
	{
	case 0: // Idle
		if (bPressed && Slot.Charges != 0) // -1 = infinite, >0 = has charges
		{
			Blink->State = 1; // HoldCheck
			Blink->HoldTimer = 0.f;
			Blink->bTargetValid = false;
			Slot.Phase = 1; // Mark active (pauses recharge)
		}
		break;

	case 1: // HoldCheck
		Blink->HoldTimer += Ctx.DeltaTime;

		if (bReleased)
		{
			// Quick-tap: instant blink
			FVector TargetFeet;
			if (ComputeBlinkTarget(MS, Ctx.Barrage, Ctx.CharacterKey, Ctx.Input, TargetFeet))
			{
				JPH::Vec3 JoltPos = CoordinateUtils::ToJoltCoordinates(FVector3d(TargetFeet));
				Ctx.FBChar->mCharacter->SetPosition(JoltPos);
				Ctx.FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());
				Ctx.FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
				if (Slot.Charges > 0) Slot.Charges--; // don't decrement infinite (-1)
				Ctx.Bridge->StateAtomics->Teleported.Fire();
				CancelSlideAbility(Ctx.Entity);
				Blink->bTeleportedThisFrame = true;
			}
			Blink->State = 0;
			Slot.Phase = 0; // Return to idle
			break;
		}

		if (Blink->HoldTimer >= MS->BlinkAimHoldThreshold)
		{
			Blink->State = 2; // Aiming
			Slot.Phase = 2;
		}
		break;

	case 2: // Aiming
		// Update target every tick
		{
			FVector TargetFeet;
			Blink->bTargetValid = ComputeBlinkTarget(MS, Ctx.Barrage, Ctx.CharacterKey, Ctx.Input, TargetFeet);
			if (Blink->bTargetValid)
			{
				Blink->TargetX = static_cast<float>(TargetFeet.X);
				Blink->TargetY = static_cast<float>(TargetFeet.Y);
				Blink->TargetZ = static_cast<float>(TargetFeet.Z);
			}
		}

		if (bReleased)
		{
			if (Blink->bTargetValid)
			{
				FVector TargetFeet(Blink->TargetX, Blink->TargetY, Blink->TargetZ);
				JPH::Vec3 JoltPos = CoordinateUtils::ToJoltCoordinates(FVector3d(TargetFeet));
				Ctx.FBChar->mCharacter->SetPosition(JoltPos);
				Ctx.FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());
				Ctx.FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
				if (Slot.Charges > 0) Slot.Charges--; // don't decrement infinite (-1)
				Ctx.Bridge->StateAtomics->Teleported.Fire();
				CancelSlideAbility(Ctx.Entity);
				Blink->bTeleportedThisFrame = true;
			}
			Blink->State = 0;
			Slot.Phase = 0; // Return to idle
			break;
		}
		break;
	}

	// TickBlink is bAlwaysTick — always return Continue (lifecycle never deactivates it)
	return EAbilityTickResult::Continue;
}

// ═══════════════════════════════════════════════════════════════
// MANTLE: HELPERS
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

// ═══════════════════════════════════════════════════════════════
// TICK MANTLE (1:1 port of StepMantleInstance + HandleHangExit)
// ═══════════════════════════════════════════════════════════════

EAbilityTickResult TickMantle(FAbilityTickContext& Ctx, FAbilitySlot& Slot)
{
	FMantleState* Mantle = Ctx.Entity.try_get_mut<FMantleState>();
	checkf(Mantle, TEXT("TickMantle: FMantleState missing on entity"));

	const FMovementStatic* MS = Ctx.MovementStatic;

	// ── Handle hang exit input BEFORE stepping FSM ──
	if (Mantle->Phase == 4) // Hanging
	{
		// Pull-up / wall-jump / let-go
		if (Ctx.bJumpPressed)
		{
			// Read camera direction from atomics
			FVector CamDir(
				Ctx.Input->CamDirX.Read(),
				Ctx.Input->CamDirY.Read(),
				Ctx.Input->CamDirZ.Read());
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
				return EAbilityTickResult::Continue;
			}
			else if (ForwardDot > 0.3f && !Mantle->bCanPullUp)
			{
				// Ceiling blocked — consume input, no action
			}
			else
			{
				// Wall jump: end mantle, apply directional force
				Ctx.FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());

				// Apply wall jump velocity in Jolt coords (ToJoltCoordinates converts cm/s → m/s)
				FVector3d ForceUE(HorizFwd * MS->WallJumpHorizontalForce + FVector::UpVector * MS->WallJumpVerticalForce);
				JPH::Vec3 JoltForce = CoordinateUtils::ToJoltCoordinates(ForceUE);
				Ctx.FBChar->mCharacter->SetLinearVelocity(JoltForce);

				// Set mantle cooldown (bug fix: was never set before)
				if (Ctx.SimState) Ctx.SimState->MantleCooldownTimer = MS->LedgeGrabCooldown;

				Mantle->Reset();
				return EAbilityTickResult::End;
			}
		}
		else if (Ctx.bCrouchEdge)
		{
			// Let go
			if (Ctx.SimState) Ctx.SimState->MantleCooldownTimer = MS->LedgeGrabCooldown;
			Mantle->Reset();
			return EAbilityTickResult::End;
		}
	}

	// ── FSM Step ──
	Mantle->Timer += Ctx.DeltaTime;

	if (Mantle->Phase == 4) // Hanging (steady state)
	{
		JPH::Vec3 HangPos(Mantle->EndX, Mantle->EndY, Mantle->EndZ);
		Ctx.FBChar->mCharacter->SetPosition(HangPos);
		Ctx.FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
		Ctx.FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());

		// Check timeout (LedgeGrabMaxDuration=0 means infinite)
		float MaxDur = MS->LedgeGrabMaxDuration;
		if (MaxDur > 0.f && Mantle->Timer > MaxDur)
		{
			if (Ctx.SimState) Ctx.SimState->MantleCooldownTimer = MS->LedgeGrabCooldown;
			Mantle->Reset();
			return EAbilityTickResult::End;
		}
		return EAbilityTickResult::Continue;
	}

	// GrabTransition, Rise, Pull, Land (Phase 0-3)
	float Alpha = FMath::Clamp(Mantle->Timer / FMath::Max(Mantle->PhaseDuration, 0.001f), 0.f, 1.f);
	float EasedAlpha = MantleEaseAlpha(Mantle->Phase, Alpha);

	JPH::Vec3 Start(Mantle->StartX, Mantle->StartY, Mantle->StartZ);
	JPH::Vec3 End(Mantle->EndX, Mantle->EndY, Mantle->EndZ);
	Ctx.FBChar->mCharacter->SetPosition(Start + (End - Start) * EasedAlpha);
	Ctx.FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
	Ctx.FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());

	// Phase completion
	if (Mantle->Timer < Mantle->PhaseDuration) return EAbilityTickResult::Continue;

	Mantle->Timer = 0.f;

	// GrabTransition → Hanging (skip Rise/Pull/Land for initial LedgeGrab)
	if (Mantle->Phase == 0 && Mantle->MantleType == 2)
	{
		Mantle->Phase = 4;
		return EAbilityTickResult::Continue;
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
			Mantle->Timer = 0.f;
			return EAbilityTickResult::Continue;
		}
		else // Vault/Mantle complete
		{
			if (Ctx.SimState) Ctx.SimState->MantleCooldownTimer = MS->LedgeGrabCooldown;
			Mantle->Reset();
			return EAbilityTickResult::End;
		}
	}
	else if (Mantle->Phase > 4)
	{
		if (Ctx.SimState) Ctx.SimState->MantleCooldownTimer = MS->LedgeGrabCooldown;
		Mantle->Reset();
		return EAbilityTickResult::End;
	}

	return EAbilityTickResult::Continue;
}

// ═══════════════════════════════════════════════════════════════
// KINETIC BLAST — instant cone impulse
// ═══════════════════════════════════════════════════════════════

EAbilityTickResult TickKineticBlast(FAbilityTickContext& Ctx, FAbilitySlot& Slot)
{
	check(Ctx.Input);
	check(Ctx.Barrage);
	check(Ctx.CharacterBridges);
	check(FBarragePrimitive::IsNotNull(Ctx.Bridge->CachedBody));

	const auto* KB = reinterpret_cast<const FKineticBlastConfig*>(Slot.ConfigData);
	checkf(KB->ConeRadius > 0.f, TEXT("TickKineticBlast: ConfigData not populated"));

	// Build cone direction from camera look direction
	const FVector3d CamDir(
		Ctx.Input->CamDirX.Read(),
		Ctx.Input->CamDirY.Read(),
		Ctx.Input->CamDirZ.Read());

	// Origin = camera position (eye level). Using feet position causes cone test
	// failures at close range: vertical offset to body centers dominates the angle.
	const FVector3d OriginUE(
		Ctx.Input->CamLocX.Read(),
		Ctx.Input->CamLocY.Read(),
		Ctx.Input->CamLocZ.Read());

	FConeImpulseParams ImpulseParams;
	ImpulseParams.OriginUE = OriginUE;
	ImpulseParams.DirectionUE = CamDir;
	ImpulseParams.Radius = KB->ConeRadius;
	ImpulseParams.HalfAngleDeg = KB->ConeHalfAngle;
	ImpulseParams.ImpulseStrength = KB->ImpulseStrength;
	ImpulseParams.FalloffExponent = KB->FalloffExponent;
	ImpulseParams.SourceKey = Ctx.CharacterKey;
	ImpulseParams.bAffectSelf = KB->bAffectSelf;
	ImpulseParams.SelfImpulseMultiplier = KB->SelfImpulseMultiplier;

	TArray<FConeImpulseHit> Hits;
	ApplyConeImpulse(ImpulseParams, Ctx.Barrage, *Ctx.CharacterBridges, &Hits);

	// Check impulse hits for fragmentable destructibles
	flecs::world World = Ctx.Entity.world();
	for (const FConeImpulseHit& Hit : Hits)
	{
		FBLet Prim = Ctx.Barrage->GetShapeRef(Hit.BodyKey);
		if (!FBarragePrimitive::IsNotNull(Prim)) continue;

		uint64 FlecsId = Prim->GetFlecsEntity();
		if (FlecsId == 0) continue;

		flecs::entity HitEntity = World.entity(FlecsId);
		if (!HitEntity.is_alive() || HitEntity.has<FTagDead>()) continue;

		const FDestructibleStatic* Destr = HitEntity.try_get<FDestructibleStatic>();
		if (!Destr || !Destr->IsValid()) continue;

		const float Threshold = Destr->Profile->FragmentationForceThreshold;
		const float ImpulseMag = Hit.ImpulseUE.Size();
		if (Threshold > 0.f && ImpulseMag < Threshold) continue;

		// Create fragmentation pair entity for FragmentationSystem
		FCollisionPair Pair;
		Pair.EntityId1 = Ctx.Entity.id(); // source attribution
		Pair.Key1 = Ctx.CharacterKey;
		Pair.EntityId2 = FlecsId;
		Pair.Key2 = Prim->KeyOutOfBarrage; // FSkeletonKey from CreatePrimitive
		Pair.ContactPoint = Hit.BodyPositionUE;

		FFragmentationData FragData;
		FragData.ImpactPoint = Hit.BodyPositionUE;
		FragData.ImpactDirection = Hit.ImpulseUE.GetSafeNormal();
		FragData.ImpactImpulse = ImpulseMag;

		World.entity()
			.set<FCollisionPair>(Pair)
			.set<FFragmentationData>(FragData)
			.add<FTagCollisionFragmentation>();
	}

	return EAbilityTickResult::End;
}
