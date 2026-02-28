// Mantle ability -- thin game-thread wrapper.
// Physics (position lerp, hang pinning, timeout) runs on sim thread via FMantleInstance
// in PrepareCharacterStep. This class handles detection, visuals, capsule, and input routing.

#include "MantleAbility.h"
#include "AbilityCapsuleHelper.h"
#include "FatumMovementComponent.h"
#include "FlecsMovementProfile.h"
#include "FlecsMovementStatic.h"
#include "FPostureStateMachine.h"
#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "CoordinateUtils.h"
#include "FBarragePrimitive.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

// =========================================================================
// GEOMETRY HELPERS (file-scope)
// =========================================================================

/** Estimated distance from hands (gripping ledge) to feet. */
static float ComputeHandToFeetDist(float StandingHalfHeight)
{
	return StandingHalfHeight * 2.f - 25.f;
}

/** Position on top of ledge, offset from wall by capsule radius + margin. */
static FVector ComputePullEndPos(const FVector& LedgeTop, const FVector& WallNormal, float CapsuleRadius)
{
	return FVector(
		LedgeTop.X + WallNormal.X * (CapsuleRadius + 10.f),
		LedgeTop.Y + WallNormal.Y * (CapsuleRadius + 10.f),
		LedgeTop.Z);
}

/** Hang position: feet below ledge, offset from wall by capsule radius + small margin. */
static FVector ComputeHangFeetPos(const FVector& LedgeTop, const FVector& WallNormal,
                                   float CapsuleRadius, float HandToFeetDist)
{
	return FVector(
		LedgeTop.X + WallNormal.X * (CapsuleRadius + 5.f),
		LedgeTop.Y + WallNormal.Y * (CapsuleRadius + 5.f),
		LedgeTop.Z - HandToFeetDist);
}

// =========================================================================
// QUERIES
// =========================================================================

bool UMantleAbility::CanActivate() const
{
	check(OwnerCMC);
	if (!OwnerCMC->MovementProfile) return false;

	// Cooldown blocks activation
	if (LedgeGrabCooldownTimer > 0.f) return false;

	// Use cached detection result
	return CachedCandidate.bValid;
}

ECharacterMoveMode UMantleAbility::GetMoveMode() const
{
	if (bInHangState)
	{
		return ECharacterMoveMode::LedgeHang;
	}

	switch (ActiveType)
	{
	case EMantleType::Vault:     return ECharacterMoveMode::Vault;
	case EMantleType::Mantle:    return ECharacterMoveMode::Mantle;
	case EMantleType::LedgeGrab: return ECharacterMoveMode::Mantle; // transition phases before hang
	}

	return ECharacterMoveMode::Mantle;
}

// =========================================================================
// ACTIVATION
// =========================================================================

void UMantleAbility::OnActivated()
{
	check(OwnerCMC && OwnerCMC->MovementProfile);
	check(CachedCandidate.bValid);

	ACharacter* Char = OwnerCMC->GetCharacterOwner();
	check(Char);

	const UFlecsMovementProfile* Profile = OwnerCMC->MovementProfile;

	// Determine mantle type from height + grounded state
	const float Height = CachedCandidate.LedgeHeight;
	const bool bGrounded = OwnerCMC->IsMovingOnGround();

	if (!bGrounded)
	{
		ActiveType = EMantleType::LedgeGrab;
	}
	else if (Height <= Profile->MantleVaultMaxHeight)
	{
		ActiveType = EMantleType::Vault;
	}
	else
	{
		ActiveType = EMantleType::Mantle;
	}

	bInHangState = false;

	// Store active ledge data (safe to read during hang/pull-up after cache is invalidated)
	ActiveLedgeTopPoint = CachedCandidate.LedgeTopPoint;
	ActiveWallNormal = CachedCandidate.WallNormal;
	bActiveCanPullUp = CachedCandidate.bCanPullUp;

	// Shrink capsule to crouch dimensions (shared helper)
	AbilityCapsuleHelper::ShrinkToCrouch(Char, OwnerCMC, Profile);

	// Force FeetToActorOffset to CrouchHalfHeight.
	// Without this, offset freezes at StandingHH when character goes airborne during mantle,
	// but capsule is only CrouchHH — actor center too high — camera jump.
	OwnerCMC->SetFeetToActorOffset(Profile->CrouchHalfHeight);

	// ── Compute target positions (UE coords) ──

	const FVector& LedgeTop = CachedCandidate.LedgeTopPoint;
	const FVector& WallNormal = CachedCandidate.WallNormal;
	const float CapsuleRadius = Char->GetCapsuleComponent()->GetScaledCapsuleRadius();

	// Current feet position (actor Z - capsule half height)
	const FVector CharFeetPos = Char->GetActorLocation() - FVector(0, 0, Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());

	FVector PullEndPos = ComputePullEndPos(LedgeTop, WallNormal, CapsuleRadius);

	// ── Set eye height target ──

	const float HandToFeetDist = ComputeHandToFeetDist(Profile->StandingHalfHeight);

	if (ActiveType == EMantleType::LedgeGrab)
	{
		HangTargetEyeHeight = HandToFeetDist - Profile->LedgeHangCameraBelowLedge;
		OwnerCMC->GetPostureSM().TargetEyeHeight = HangTargetEyeHeight;
	}
	else
	{
		OwnerCMC->GetPostureSM().TargetEyeHeight = Profile->MantleEyeHeight;
	}

	// ── Build FMantleInstance and send to sim thread ──

	// Compute Jolt positions
	JPH::Vec3 JoltStart = CoordinateUtils::ToJoltCoordinates(FVector3d(CharFeetPos));
	JPH::Vec3 JoltPullEnd = CoordinateUtils::ToJoltCoordinates(FVector3d(PullEndPos));

	// Wall normal: unit vector, axis swap only (no scale)
	JPH::Vec3 JoltWallNormal = CoordinateUtils::ToJoltUnitVector(FVector3d(WallNormal));

	UFlecsArtillerySubsystem* Sub = OwnerCMC->GetFlecsSubsystem();
	FSkeletonKey Key = OwnerCMC->GetCharacterEntityKey();
	check(Sub && Key.IsValid());

	if (ActiveType == EMantleType::LedgeGrab)
	{
		// LedgeGrab: GrabTransition phase (lerp to hang position, then pin)
		FVector HangFeetPos = ComputeHangFeetPos(LedgeTop, WallNormal, CapsuleRadius, HandToFeetDist);

		JPH::Vec3 JoltHangPos = CoordinateUtils::ToJoltCoordinates(FVector3d(HangFeetPos));

		float GrabDuration = Profile->LedgeGrabTransitionDuration;

		// Capture values for lambda
		float SX = JoltStart.GetX(), SY = JoltStart.GetY(), SZ = JoltStart.GetZ();
		float EX = JoltHangPos.GetX(), EY = JoltHangPos.GetY(), EZ = JoltHangPos.GetZ();
		float PEX = JoltPullEnd.GetX(), PEY = JoltPullEnd.GetY(), PEZ = JoltPullEnd.GetZ();
		float WNX = JoltWallNormal.GetX(), WNZ = JoltWallNormal.GetZ();
		float PullDur = Profile->PullUpDuration;
		float LandDur = Profile->MantleLandDuration;

		Sub->EnqueueCommand([Sub, Key, SX, SY, SZ, EX, EY, EZ,
		                     PEX, PEY, PEZ, WNX, WNZ,
		                     GrabDuration, PullDur, LandDur]()
		{
			flecs::entity Entity = Sub->GetEntityForBarrageKey(Key);
			if (!Entity.is_valid()) return;

			FMantleInstance MI;
			MI.StartX = SX; MI.StartY = SY; MI.StartZ = SZ;
			MI.EndX = EX; MI.EndY = EY; MI.EndZ = EZ;
			MI.PullEndX = PEX; MI.PullEndY = PEY; MI.PullEndZ = PEZ;
			MI.WallNormalX = WNX; MI.WallNormalZ = WNZ;
			MI.Timer = 0.f;
			MI.PhaseDuration = GrabDuration;
			MI.PullDuration = PullDur;
			MI.LandDuration = LandDur;
			MI.Phase = 0; // GrabTransition
			MI.MantleType = 2; // LedgeGrab
			Entity.set<FMantleInstance>(MI);
		});
	}
	else
	{
		// Vault/Mantle: Rise phase (lift to ledge top height), then Pull (forward onto ledge)

		// Rise target: same XY as character, Z at ledge top
		FVector RiseEndPos = CharFeetPos;
		RiseEndPos.Z = LedgeTop.Z;

		JPH::Vec3 JoltRiseEnd = CoordinateUtils::ToJoltCoordinates(FVector3d(RiseEndPos));

		float RiseDuration = Profile->MantleRiseDuration;
		float PullDur = Profile->MantlePullDuration;
		float LandDur = Profile->MantleLandDuration;
		uint8 MType = (ActiveType == EMantleType::Vault) ? 0 : 1;

		// Capture values for lambda
		float SX = JoltStart.GetX(), SY = JoltStart.GetY(), SZ = JoltStart.GetZ();
		float REX = JoltRiseEnd.GetX(), REY = JoltRiseEnd.GetY(), REZ = JoltRiseEnd.GetZ();
		float PEX = JoltPullEnd.GetX(), PEY = JoltPullEnd.GetY(), PEZ = JoltPullEnd.GetZ();
		float WNX = JoltWallNormal.GetX(), WNZ = JoltWallNormal.GetZ();

		Sub->EnqueueCommand([Sub, Key, SX, SY, SZ, REX, REY, REZ,
		                     PEX, PEY, PEZ, WNX, WNZ,
		                     RiseDuration, PullDur, LandDur, MType]()
		{
			flecs::entity Entity = Sub->GetEntityForBarrageKey(Key);
			if (!Entity.is_valid()) return;

			FMantleInstance MI;
			MI.StartX = SX; MI.StartY = SY; MI.StartZ = SZ;
			MI.EndX = REX; MI.EndY = REY; MI.EndZ = REZ;
			MI.PullEndX = PEX; MI.PullEndY = PEY; MI.PullEndZ = PEZ;
			MI.WallNormalX = WNX; MI.WallNormalZ = WNZ;
			MI.Timer = 0.f;
			MI.PhaseDuration = RiseDuration;
			MI.PullDuration = PullDur;
			MI.LandDuration = LandDur;
			MI.Phase = 1; // Rise (skip GrabTransition for vault/mantle)
			MI.MantleType = MType;
			Entity.set<FMantleInstance>(MI);
		});
	}

	// Invalidate cached candidate after activation
	CachedCandidate.bValid = false;
}

// =========================================================================
// TICK (game thread -- eye height interpolation only)
// =========================================================================

void UMantleAbility::OnTick(float DeltaTime)
{
	check(OwnerCMC && OwnerCMC->MovementProfile);

	const UFlecsMovementProfile* Profile = OwnerCMC->MovementProfile;
	FPostureStateMachine& SM = OwnerCMC->GetPostureSM();

	// Cooldown is ticked externally via TickCooldown() from TickPostureAndEffects

	// Eye height interpolation
	SM.CurrentEyeHeight = FMath::FInterpTo(
		SM.CurrentEyeHeight, SM.TargetEyeHeight,
		DeltaTime, Profile->MantleTransitionSpeed);
}

// =========================================================================
// DEACTIVATION (capsule/posture restoration -- mirrors SlideAbility)
// =========================================================================

void UMantleAbility::OnDeactivated()
{
	check(OwnerCMC && OwnerCMC->MovementProfile);

	ACharacter* Char = OwnerCMC->GetCharacterOwner();
	check(Char);

	const UFlecsMovementProfile* Profile = OwnerCMC->MovementProfile;

	// Tell sim thread: mantle ended (remove FMantleInstance).
	// May already be removed by sim thread timeout -- EnqueueCommand is safe either way.
	UFlecsArtillerySubsystem* Sub = OwnerCMC->GetFlecsSubsystem();
	FSkeletonKey Key = OwnerCMC->GetCharacterEntityKey();
	if (Sub && Key.IsValid())
	{
		Sub->EnqueueCommand([Sub, Key]()
		{
			flecs::entity Entity = Sub->GetEntityForBarrageKey(Key);
			if (Entity.is_valid())
			{
				Entity.remove<FMantleInstance>();
			}
		});
	}

	// Restore capsule to best-fit posture (shared helper)
	AbilityCapsuleHelper::RestoreFromCrouch(Char, OwnerCMC, Profile);

	// Force FeetToActorOffset to match new capsule
	float HH = Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	OwnerCMC->SetFeetToActorOffset(HH);

	// Reset state
	bInHangState = false;
	LedgeGrabCooldownTimer = Profile->LedgeGrabCooldown;
	CachedCandidate.bValid = false;
}

// =========================================================================
// INPUT INTERCEPTION
// =========================================================================

bool UMantleAbility::HandleJumpRequest()
{
	check(OwnerCMC && OwnerCMC->MovementProfile);

	if (!bInHangState)
	{
		// During Rise/Pull/Land phases: consume the jump, do nothing.
		// Can't jump during an active mantle/vault transition.
		return true;
	}

	// Hanging -- determine exit action based on forward input
	ACharacter* Char = OwnerCMC->GetCharacterOwner();
	check(Char);

	APlayerController* PC = Cast<APlayerController>(Char->Controller);
	if (!PC) return true;

	// Use active ledge data (not CachedCandidate — may be invalidated)
	FVector CameraFwd = PC->GetControlRotation().Vector();
	FVector HorizFwd = FVector(CameraFwd.X, CameraFwd.Y, 0.f).GetSafeNormal();

	// Check if looking toward the wall — indicates intent to climb
	float ForwardDot = FVector::DotProduct(HorizFwd, -ActiveWallNormal);
	bool bForwardHeld = ForwardDot > 0.3f;

	if (bForwardHeld && bActiveCanPullUp)
	{
		RequestPullUp();
		return true;
	}
	else if (bForwardHeld && !bActiveCanPullUp)
	{
		// Ceiling blocked, can't pull up. Jump consumed, no action.
		return true;
	}
	else
	{
		// Not looking at wall -- wall jump in camera direction
		RequestWallJump(CameraFwd);
		return true;
	}
}

bool UMantleAbility::HandleCrouchInput(bool bPressed)
{
	if (bPressed)
	{
		RequestLetGo();
	}
	return true;
}

// =========================================================================
// HANG EXIT: PULL UP
// =========================================================================

void UMantleAbility::RequestPullUp()
{
	check(OwnerCMC && OwnerCMC->MovementProfile);

	ACharacter* Char = OwnerCMC->GetCharacterOwner();
	check(Char);

	const UFlecsMovementProfile* Profile = OwnerCMC->MovementProfile;
	UFlecsArtillerySubsystem* Sub = OwnerCMC->GetFlecsSubsystem();
	FSkeletonKey Key = OwnerCMC->GetCharacterEntityKey();
	check(Sub && Key.IsValid());

	// Compute pull-up target from active ledge data (not CachedCandidate — may be invalidated)
	const FVector& LedgeTop = ActiveLedgeTopPoint;
	const FVector& WallNormal = ActiveWallNormal;
	float CapsuleRadius = Char->GetCapsuleComponent()->GetScaledCapsuleRadius();
	float HandToFeetDist = ComputeHandToFeetDist(Profile->StandingHalfHeight);

	FVector PullEndPos = ComputePullEndPos(LedgeTop, WallNormal, CapsuleRadius);
	JPH::Vec3 JoltPullEnd = CoordinateUtils::ToJoltCoordinates(FVector3d(PullEndPos));

	// Rise target: same XY as hang position, Z at ledge top
	FVector HangFeetPos = ComputeHangFeetPos(LedgeTop, WallNormal, CapsuleRadius, HandToFeetDist);
	FVector RiseEndPos = FVector(HangFeetPos.X, HangFeetPos.Y, LedgeTop.Z);

	JPH::Vec3 JoltRiseEnd = CoordinateUtils::ToJoltCoordinates(FVector3d(RiseEndPos));

	float RiseDur = Profile->PullUpDuration;
	float PullDur = Profile->MantlePullDuration;
	float LandDur = Profile->MantleLandDuration;

	// Capture Jolt coords for lambda
	float REX = JoltRiseEnd.GetX(), REY = JoltRiseEnd.GetY(), REZ = JoltRiseEnd.GetZ();
	float PEX = JoltPullEnd.GetX(), PEY = JoltPullEnd.GetY(), PEZ = JoltPullEnd.GetZ();

	Sub->EnqueueCommand([Sub, Key, REX, REY, REZ, PEX, PEY, PEZ,
	                     RiseDur, PullDur, LandDur]()
	{
		flecs::entity Entity = Sub->GetEntityForBarrageKey(Key);
		if (!Entity.is_valid()) return;

		// CRITICAL null-check: sim-thread timeout may have removed the component
		// between game-thread decision and EnqueueCommand execution.
		FMantleInstance* MI = Entity.try_get_mut<FMantleInstance>();
		if (!MI) return;

		// Set current hang position as Rise start
		MI->StartX = MI->EndX;
		MI->StartY = MI->EndY;
		MI->StartZ = MI->EndZ;
		// Rise target: up to ledge level
		MI->EndX = REX; MI->EndY = REY; MI->EndZ = REZ;
		MI->PullEndX = PEX; MI->PullEndY = PEY; MI->PullEndZ = PEZ;
		MI->Timer = 0.f;
		MI->PhaseDuration = RiseDur;
		MI->PullDuration = PullDur;
		MI->LandDuration = LandDur;
		MI->Phase = 1; // Rise
		MI->MantleType = 1; // Treat as Mantle — sim thread removes at Phase 4 instead of re-hanging
	});

	// Update eye height for pull-up (transition to mantle eye height)
	OwnerCMC->GetPostureSM().TargetEyeHeight = Profile->MantleEyeHeight;
}

// =========================================================================
// HANG EXIT: WALL JUMP
// =========================================================================

void UMantleAbility::RequestWallJump(const FVector& CameraForward)
{
	check(OwnerCMC && OwnerCMC->MovementProfile);

	const UFlecsMovementProfile* Profile = OwnerCMC->MovementProfile;

	// Project camera forward onto horizontal plane
	FVector HorizDir = FVector(CameraForward.X, CameraForward.Y, 0.f).GetSafeNormal();
	if (HorizDir.IsNearlyZero())
	{
		HorizDir = ActiveWallNormal; // fallback: push away from wall
	}

	float HForce = Profile->WallJumpHorizontalForce;
	float VForce = Profile->WallJumpVerticalForce;
	FVector JumpDir = HorizDir * HForce + FVector::UpVector * VForce;

	// Capture before deactivation
	TSharedPtr<FBarragePrimitive> Body = OwnerCMC->GetCharacterBarrageBody();
	UFlecsArtillerySubsystem* Sub = OwnerCMC->GetFlecsSubsystem();

	// Deactivate ability (restores capsule/posture, enqueues remove<FMantleInstance>)
	OwnerCMC->DeactivateAbility();

	// Enqueue wall-jump force AFTER the remove<FMantleInstance> command (FIFO queue).
	// This guarantees force is applied after position pin is removed — StepCharacter honors it.
	if (Sub && FBarragePrimitive::IsNotNull(Body))
	{
		FVector3d JumpDirCapture(JumpDir);
		TSharedPtr<FBarragePrimitive> BodyCapture = Body;
		Sub->EnqueueCommand([JumpDirCapture, BodyCapture]()
		{
			FBarragePrimitive::ApplyForce(JumpDirCapture, BodyCapture, PhysicsInputType::OtherForce);
		});
	}
}

// =========================================================================
// HANG EXIT: LET GO
// =========================================================================

void UMantleAbility::RequestLetGo()
{
	check(OwnerCMC);
	OwnerCMC->DeactivateAbility();
}

// =========================================================================
// DETECTION (delegates to stateless FLedgeDetector)
// =========================================================================

void UMantleAbility::PerformDetection(const FVector& CharFeetPos, const FVector& LookDir,
                                       float CapsuleRadius, float CapsuleHalfHeight)
{
	CachedCandidate.bValid = false;

	if (!OwnerCMC || !OwnerCMC->MovementProfile) return;

	const UFlecsMovementProfile* Profile = OwnerCMC->MovementProfile;
	ACharacter* Char = OwnerCMC->GetCharacterOwner();
	if (!Char) return;

	UBarrageDispatch* Barrage = Char->GetWorld()->GetSubsystem<UBarrageDispatch>();
	if (!Barrage) return;

	FSkeletonKey CharKey = OwnerCMC->GetCharacterEntityKey();
	if (!CharKey.IsValid()) return;

	const bool bGrounded = OwnerCMC->IsMovingOnGround();
	const float MaxReachHeight = bGrounded ? Profile->MantleMaxHeight : Profile->LedgeGrabMaxHeight;

	FLedgeDetector::Detect(CharFeetPos, LookDir, CapsuleRadius, CapsuleHalfHeight,
	                       MaxReachHeight, Profile, Barrage, CharKey,
	                       CachedCandidate);
}
