// Mantle ability -- thin game-thread wrapper.
// Physics (position lerp, hang pinning, timeout) runs on sim thread via FMantleInstance
// in PrepareCharacterStep. This class handles detection, visuals, capsule, and input routing.

#include "MantleAbility.h"
#include "FatumMovementComponent.h"
#include "FlecsMovementProfile.h"
#include "FlecsMovementStatic.h"
#include "FPostureStateMachine.h"
#include "FlecsCharacter.h"
#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "CoordinateUtils.h"
#include "FBarragePrimitive.h"
#include "PhysicsFilters/FastObjectLayerFilters.h"
#include "EPhysicsLayer.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

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
	AFlecsCharacter* FlecsChar = Cast<AFlecsCharacter>(Char);
	check(FlecsChar);

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

	// ── Shrink capsule to crouch dimensions ──

	float OldHH = Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Char->GetCapsuleComponent()->SetCapsuleSize(
		Profile->CrouchRadius, Profile->CrouchHalfHeight, true);

	float HeightDelta = Profile->CrouchHalfHeight - OldHH;
	if (!FMath::IsNearlyZero(HeightDelta))
	{
		FVector Loc = Char->GetActorLocation();
		Loc.Z += HeightDelta;
		Char->SetActorLocation(Loc);
		OwnerCMC->GetPostureSM().CurrentEyeHeight -= HeightDelta;
	}

	// Force FeetToActorOffset to CrouchHalfHeight.
	// Without this, offset freezes at StandingHH when character goes airborne during mantle,
	// but capsule is only CrouchHH -- actor center too high -- camera jump.
	FlecsChar->FeetToActorOffset = Profile->CrouchHalfHeight;

	// Set posture to Crouching so external queries see correct state
	OwnerCMC->GetPostureSM().CurrentPosture = ECharacterPosture::Crouching;

	// Sync Barrage capsule shape
	OwnerCMC->BroadcastPostureChanged(ECharacterPosture::Crouching);

	// ── Compute target positions (UE coords) ──

	const FVector& LedgeTop = CachedCandidate.LedgeTopPoint;
	const FVector& WallNormal = CachedCandidate.WallNormal;
	const float CapsuleRadius = Char->GetCapsuleComponent()->GetScaledCapsuleRadius();

	// Current feet position (actor Z - capsule half height)
	const FVector CharFeetPos = Char->GetActorLocation() - FVector(0, 0, Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());

	// Pull target: on top of ledge, offset from wall by capsule radius + margin
	FVector PullEndPos;
	PullEndPos.X = LedgeTop.X + WallNormal.X * (CapsuleRadius + 10.f);
	PullEndPos.Y = LedgeTop.Y + WallNormal.Y * (CapsuleRadius + 10.f);
	PullEndPos.Z = LedgeTop.Z;

	// ── Set eye height target ──

	if (ActiveType == EMantleType::LedgeGrab)
	{
		// Hang eye height: relative to hang position
		// HandToFeetDist = StandingHalfHeight * 2 - 25cm (arms extend above head)
		float HandToFeetDist = Profile->StandingHalfHeight * 2.f - 25.f;
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

	UFlecsArtillerySubsystem* Sub = FlecsChar->GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	FSkeletonKey Key = FlecsChar->GetEntityKey();
	check(Sub && Key.IsValid());

	if (ActiveType == EMantleType::LedgeGrab)
	{
		// LedgeGrab: GrabTransition phase (lerp to hang position, then pin)
		float HandToFeetDist = Profile->StandingHalfHeight * 2.f - 25.f;
		FVector HangFeetPos;
		HangFeetPos.X = LedgeTop.X + WallNormal.X * (CapsuleRadius + 5.f);
		HangFeetPos.Y = LedgeTop.Y + WallNormal.Y * (CapsuleRadius + 5.f);
		HangFeetPos.Z = LedgeTop.Z - HandToFeetDist;

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
	AFlecsCharacter* FlecsChar = Cast<AFlecsCharacter>(Char);
	if (FlecsChar)
	{
		UFlecsArtillerySubsystem* Sub = FlecsChar->GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
		FSkeletonKey Key = FlecsChar->GetEntityKey();
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
	}

	// Determine target posture after mantle
	ECharacterPosture TargetPosture;
	if (OwnerCMC->CanExpandToHeight(Profile->StandingHalfHeight))
	{
		TargetPosture = ECharacterPosture::Standing;
	}
	else
	{
		TargetPosture = ECharacterPosture::Crouching;
	}

	// Restore capsule size
	float OldHH = Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	float R, HH;
	Profile->GetCapsuleForPosture(TargetPosture, R, HH);
	Char->GetCapsuleComponent()->SetCapsuleSize(R, HH, true);

	// Adjust actor Z for height delta (only if grounded after mantle completes)
	float HeightDelta = HH - OldHH;
	if (!FMath::IsNearlyZero(HeightDelta) && OwnerCMC->IsMovingOnGround())
	{
		FVector Loc = Char->GetActorLocation();
		Loc.Z += HeightDelta;
		Char->SetActorLocation(Loc);
		OwnerCMC->GetPostureSM().CurrentEyeHeight -= HeightDelta;
	}

	// Force FeetToActorOffset to match new capsule
	if (FlecsChar)
	{
		FlecsChar->FeetToActorOffset = HH;
	}

	// Sync PostureSM state
	FPostureStateMachine& SM = OwnerCMC->GetPostureSM();
	SM.CurrentPosture = TargetPosture;
	SM.TargetEyeHeight = Profile->GetEyeHeightForPosture(TargetPosture);

	if (TargetPosture == ECharacterPosture::Crouching)
	{
		SM.RequestCrouch(true, Profile);
	}
	else
	{
		SM.ForceClearCrouch();
	}

	// Sync Barrage capsule shape
	OwnerCMC->BroadcastPostureChanged(TargetPosture);

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

// =========================================================================
// HANG EXIT: PULL UP
// =========================================================================

void UMantleAbility::RequestPullUp()
{
	check(OwnerCMC && OwnerCMC->MovementProfile);

	ACharacter* Char = OwnerCMC->GetCharacterOwner();
	check(Char);

	AFlecsCharacter* FlecsChar = Cast<AFlecsCharacter>(Char);
	check(FlecsChar);

	const UFlecsMovementProfile* Profile = OwnerCMC->MovementProfile;
	UFlecsArtillerySubsystem* Sub = FlecsChar->GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	FSkeletonKey Key = FlecsChar->GetEntityKey();
	check(Sub && Key.IsValid());

	// Compute pull-up target from active ledge data (not CachedCandidate — may be invalidated)
	const FVector& LedgeTop = ActiveLedgeTopPoint;
	const FVector& WallNormal = ActiveWallNormal;
	float CapsuleRadius = Char->GetCapsuleComponent()->GetScaledCapsuleRadius();

	FVector PullEndPos;
	PullEndPos.X = LedgeTop.X + WallNormal.X * (CapsuleRadius + 10.f);
	PullEndPos.Y = LedgeTop.Y + WallNormal.Y * (CapsuleRadius + 10.f);
	PullEndPos.Z = LedgeTop.Z;

	JPH::Vec3 JoltPullEnd = CoordinateUtils::ToJoltCoordinates(FVector3d(PullEndPos));

	// Rise target: same XY as hang position, Z at ledge top
	// The hang position XY is already stored in FMantleInstance.EndX/Z, but we compute fresh here
	float HandToFeetDist = Profile->StandingHalfHeight * 2.f - 25.f;
	FVector HangFeetPos;
	HangFeetPos.X = LedgeTop.X + WallNormal.X * (CapsuleRadius + 5.f);
	HangFeetPos.Y = LedgeTop.Y + WallNormal.Y * (CapsuleRadius + 5.f);
	HangFeetPos.Z = LedgeTop.Z - HandToFeetDist;

	FVector RiseEndPos = HangFeetPos;
	RiseEndPos.Z = LedgeTop.Z;

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
	ACharacter* Char = OwnerCMC->GetCharacterOwner();
	AFlecsCharacter* FlecsChar = Char ? Cast<AFlecsCharacter>(Char) : nullptr;
	TSharedPtr<FBarragePrimitive> Body = FlecsChar ? FlecsChar->CachedBarrageBody : nullptr;
	UFlecsArtillerySubsystem* Sub = FlecsChar ? FlecsChar->GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>() : nullptr;

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
// DETECTION (5-phase algorithm using Barrage raycasts)
// =========================================================================

void UMantleAbility::PerformDetection(const FVector& CharFeetPos, const FVector& LookDir,
                                       float CapsuleRadius, float CapsuleHalfHeight)
{
	CachedCandidate.bValid = false;

	if (!OwnerCMC || !OwnerCMC->MovementProfile) return;

	const UFlecsMovementProfile* Profile = OwnerCMC->MovementProfile;
	ACharacter* Char = OwnerCMC->GetCharacterOwner();
	if (!Char) return;

	// Get Barrage subsystem for raycasts
	UBarrageDispatch* Barrage = Char->GetWorld()->GetSubsystem<UBarrageDispatch>();
	if (!Barrage) return;

	AFlecsCharacter* FlecsChar = Cast<AFlecsCharacter>(Char);
	if (!FlecsChar) return;

	// ── Determine max reach height based on grounded state ──
	const bool bGrounded = OwnerCMC->IsMovingOnGround();
	const float MaxReachHeight = bGrounded ? Profile->MantleMaxHeight : Profile->LedgeGrabMaxHeight;

	// ── Set up filters ──
	// Include static geometry + dynamic objects (active debris is MOVING layer).
	// MOVING also hits items/characters, but 5-phase validation rejects false positives.
	FastIncludeObjectLayerFilter LedgeFilter({EPhysicsLayer::NON_MOVING, EPhysicsLayer::MOVING});

	// Broad phase filter for CAST_QUERY layer
	auto BPFilter = Barrage->GetDefaultBroadPhaseLayerFilter(Layers::CAST_QUERY);

	// Ignore own character body
	FBarrageKey CharBarrageKey = Barrage->GetBarrageKeyFromSkeletonKey(FlecsChar->GetEntityKey());
	auto BodyFilter = Barrage->GetFilterToIgnoreSingleBody(CharBarrageKey);

	// Horizontal look direction (no vertical component)
	FVector HorizLook = FVector(LookDir.X, LookDir.Y, 0.f).GetSafeNormal();
	if (HorizLook.IsNearlyZero()) return; // Can't detect without look direction

	TSharedPtr<FHitResult> HitResult = MakeShared<FHitResult>();

	// ═══════════════════════════════════════════════════════════════
	// PHASE 1: FORWARD WALL CHECK (SphereCast)
	// Origin: character chest height
	// Direction: camera forward projected onto horizontal plane
	// ═══════════════════════════════════════════════════════════════

	// Chest height = feet + eye height * 0.5 (roughly mid-torso)
	FVector ChestOrigin = CharFeetPos;
	ChestOrigin.Z += Profile->StandingEyeHeight * 0.5f;

	// CRITICAL: SphereCast Radius is in Jolt meters, NOT UE cm
	double JoltDetectionRadius = Profile->LedgeGrabDetectionRadius / 100.0;

	UE_LOG(LogTemp, Warning, TEXT("Mantle P1: Origin=(%.1f,%.1f,%.1f) Dir=(%.2f,%.2f,%.2f) Reach=%.1f Radius=%.4f FeetZ=%.1f"),
		ChestOrigin.X, ChestOrigin.Y, ChestOrigin.Z,
		HorizLook.X, HorizLook.Y, HorizLook.Z,
		Profile->MantleForwardReach, JoltDetectionRadius, CharFeetPos.Z);

	Barrage->SphereCast(
		JoltDetectionRadius,
		Profile->MantleForwardReach,
		ChestOrigin,
		HorizLook,
		HitResult,
		BPFilter, LedgeFilter, BodyFilter);

	if (!HitResult->bBlockingHit)
	{
		UE_LOG(LogTemp, Warning, TEXT("Mantle P1: FAIL - no wall hit"));
		return;
	}

	FVector WallHitPoint = HitResult->ImpactPoint;
	FVector WallNormal = HitResult->ImpactNormal;

	UE_LOG(LogTemp, Warning, TEXT("Mantle P1: HIT at (%.1f,%.1f,%.1f) Normal=(%.2f,%.2f,%.2f)"),
		WallHitPoint.X, WallHitPoint.Y, WallHitPoint.Z,
		WallNormal.X, WallNormal.Y, WallNormal.Z);

	// Validate wall normal is roughly horizontal (not floor/ceiling)
	if (FMath::Abs(WallNormal.Z) > 0.5f)
	{
		UE_LOG(LogTemp, Warning, TEXT("Mantle P1: FAIL - normal too vertical (Z=%.2f)"), WallNormal.Z);
		return;
	}

	// Ensure wall normal points away from character
	WallNormal = FVector(WallNormal.X, WallNormal.Y, 0.f).GetSafeNormal();
	if (WallNormal.IsNearlyZero()) return;

	// Facing check: must be looking toward the wall
	float FacingDot = FVector::DotProduct(HorizLook, -WallNormal);
	if (FacingDot < 0.3f)
	{
		UE_LOG(LogTemp, Warning, TEXT("Mantle P1: FAIL - facing dot=%.2f < 0.3"), FacingDot);
		return;
	}

	// ═══════════════════════════════════════════════════════════════
	// PHASE 2: HIGH FORWARD PROBE (SphereCast)
	// Cast forward at MaxReachHeight above feet to check if wall
	// extends that high. Origin is at CHARACTER position (guaranteed
	// outside any wall body). If hit → wall too tall → reject.
	// If no hit → wall shorter → Phase 3 downward cast is safe.
	// ═══════════════════════════════════════════════════════════════

	// Margin above MaxReachHeight: compensates for SphereCast radius + boundary precision.
	// Phase 3 height validation enforces the actual MantleMaxHeight limit precisely.
	const float ProbeMargin = 15.f; // cm

	FVector Phase2Origin = CharFeetPos;
	Phase2Origin.Z += MaxReachHeight + ProbeMargin;

	HitResult = MakeShared<FHitResult>();

	UE_LOG(LogTemp, Warning, TEXT("Mantle P2: Origin=(%.1f,%.1f,%.1f) Dir=(%.2f,%.2f,%.2f) Reach=%.1f MaxReach=%.1f"),
		Phase2Origin.X, Phase2Origin.Y, Phase2Origin.Z,
		HorizLook.X, HorizLook.Y, HorizLook.Z,
		Profile->MantleForwardReach, MaxReachHeight);

	Barrage->SphereCast(
		JoltDetectionRadius,
		Profile->MantleForwardReach,
		Phase2Origin,
		HorizLook,
		HitResult,
		BPFilter, LedgeFilter, BodyFilter);

	if (HitResult->bBlockingHit)
	{
		UE_LOG(LogTemp, Warning, TEXT("Mantle P2: FAIL - wall extends to MaxReachHeight (hit at Z=%.1f)"), HitResult->ImpactPoint.Z);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Mantle P2: PASS - wall shorter than MaxReachHeight"));

	// ═══════════════════════════════════════════════════════════════
	// PHASE 3: DOWNWARD SURFACE (SphereCast)
	// Find the ledge top surface. Origin is 15cm into the wall face
	// at MaxReachHeight+margin Z — guaranteed ABOVE the wall body by Phase 2.
	// ═══════════════════════════════════════════════════════════════

	FVector Phase3Origin = WallHitPoint - WallNormal * 15.f; // 15cm into wall cross-section
	Phase3Origin.Z = CharFeetPos.Z + MaxReachHeight + ProbeMargin; // above the wall (Phase 2 confirmed)

	float Phase3Distance = MaxReachHeight + ProbeMargin - Profile->MantleMinHeight;
	if (Phase3Distance <= 0.f) return;

	// Small sphere for surface detection (5cm radius in Jolt meters)
	double SmallRadius = 5.0 / 100.0;

	UE_LOG(LogTemp, Warning, TEXT("Mantle P3: Origin=(%.1f,%.1f,%.1f) DownDist=%.1f MinH=%.1f"),
		Phase3Origin.X, Phase3Origin.Y, Phase3Origin.Z, Phase3Distance, Profile->MantleMinHeight);

	HitResult = MakeShared<FHitResult>();
	Barrage->SphereCast(
		SmallRadius,
		Phase3Distance,
		Phase3Origin,
		-FVector::UpVector, // straight down
		HitResult,
		BPFilter, LedgeFilter, BodyFilter);

	if (!HitResult->bBlockingHit)
	{
		UE_LOG(LogTemp, Warning, TEXT("Mantle P3: FAIL - no surface found"));
		return;
	}

	FVector LedgeTopPoint = HitResult->ImpactPoint;
	FVector LedgeNormal = HitResult->ImpactNormal;

	UE_LOG(LogTemp, Warning, TEXT("Mantle P3: HIT surface at (%.1f,%.1f,%.1f) Normal=(%.2f,%.2f,%.2f)"),
		LedgeTopPoint.X, LedgeTopPoint.Y, LedgeTopPoint.Z,
		LedgeNormal.X, LedgeNormal.Y, LedgeNormal.Z);

	// Validate ledge surface is roughly flat (slope < ~45 degrees)
	if (FVector::DotProduct(LedgeNormal, FVector::UpVector) < 0.7f)
	{
		UE_LOG(LogTemp, Warning, TEXT("Mantle P3: FAIL - surface not flat (dot=%.2f)"),
			FVector::DotProduct(LedgeNormal, FVector::UpVector));
		return;
	}

	// Validate ledge height is within acceptable range
	float LedgeHeight = LedgeTopPoint.Z - CharFeetPos.Z;
	if (LedgeHeight < Profile->MantleMinHeight || LedgeHeight > MaxReachHeight)
	{
		UE_LOG(LogTemp, Warning, TEXT("Mantle P3: FAIL - height %.1f outside [%.1f, %.1f]"),
			LedgeHeight, Profile->MantleMinHeight, MaxReachHeight);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Mantle P3: PASS - ledge height=%.1f cm"), LedgeHeight);

	// ═══════════════════════════════════════════════════════════════
	// PHASE 4: DEPTH CHECK (CastRay)
	// Ensure the ledge has enough depth to stand on
	// ═══════════════════════════════════════════════════════════════

	FVector Phase4Origin = LedgeTopPoint + FVector(0, 0, 5.f); // slightly above surface
	// Direction = into wall (opposite of wall normal) * min depth distance
	FVector DepthDir = -WallNormal * Profile->LedgeGrabMinLedgeDepth;

	HitResult = MakeShared<FHitResult>();
	Barrage->CastRay(
		Phase4Origin,
		DepthDir,
		BPFilter, LedgeFilter, BodyFilter,
		HitResult);

	// If hit, ledge is too thin -- reject
	if (HitResult->bBlockingHit)
	{
		UE_LOG(LogTemp, Warning, TEXT("Mantle P4: FAIL - ledge too thin (hit at dist)"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Mantle P4: PASS - ledge has depth"));

	// ═══════════════════════════════════════════════════════════════
	// PHASE 5: TOP CLEARANCE (CastRay)
	// Check if there's room to stand on the ledge after pull-up
	// ═══════════════════════════════════════════════════════════════

	FVector Phase5Origin = LedgeTopPoint + WallNormal * (CapsuleRadius + 10.f);
	float Phase5Distance = CapsuleHalfHeight * 2.f;
	FVector Phase5Dir = FVector::UpVector * Phase5Distance;

	HitResult = MakeShared<FHitResult>();
	Barrage->CastRay(
		Phase5Origin,
		Phase5Dir,
		BPFilter, LedgeFilter, BodyFilter,
		HitResult);

	bool bCanPullUp = !HitResult->bBlockingHit;

	UE_LOG(LogTemp, Warning, TEXT("Mantle P5: CanPullUp=%d"), bCanPullUp);

	// ═══════════════════════════════════════════════════════════════
	// CACHE RESULT
	// ═══════════════════════════════════════════════════════════════

	UE_LOG(LogTemp, Warning, TEXT("Mantle DETECTED: Type=%s Height=%.1f LedgeTop=(%.1f,%.1f,%.1f)"),
		(LedgeHeight <= Profile->MantleVaultMaxHeight) ? TEXT("Vault") : TEXT("Mantle"),
		LedgeHeight, LedgeTopPoint.X, LedgeTopPoint.Y, LedgeTopPoint.Z);

	CachedCandidate.LedgeTopPoint = LedgeTopPoint;
	CachedCandidate.WallHitPoint = WallHitPoint;
	CachedCandidate.WallNormal = WallNormal;
	CachedCandidate.LedgeHeight = LedgeHeight;
	CachedCandidate.bCanPullUp = bCanPullUp;
	CachedCandidate.bValid = true;
}
