// UFatumMovementComponent — posture, capsule management, camera effects.
// Ability logic (slide, mantle, blink, jump) runs on sim thread.
// This component reads sim-thread state via atomics and manages game-thread visuals.

#include "FatumMovementComponent.h"
#include "AbilityCapsuleHelper.h"
#include "FlecsCharacter.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsMovementProfile.h"
#include "FPostureStateMachine.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

UFatumMovementComponent::UFatumMovementComponent()
{
	NavAgentProps.bCanCrouch = true;
}

void UFatumMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();
	ApplyProfile();
}

void UFatumMovementComponent::UninitializeComponent()
{
	Super::UninitializeComponent();
}

void UFatumMovementComponent::ApplyProfile()
{
	if (!MovementProfile)
	{
		UE_LOG(LogTemp, Warning, TEXT("UFatumMovementComponent::ApplyProfile: No MovementProfile set!"));
		return;
	}

	PostureSM.CurrentEyeHeight = MovementProfile->StandingEyeHeight;
	PostureSM.TargetEyeHeight = MovementProfile->StandingEyeHeight;
}

void UFatumMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	// WARNING: Do NOT add logic here.
	// All movement state ticking is driven by AFlecsCharacter::Tick() to ensure correct ordering:
	//   ReadBarragePosition → TickPostureAndEffects → Camera Update.
	// Velocity/MovementMode are set in ApplyBarrageSync (before TickPostureAndEffects).
}

// ═══════════════════════════════════════════════════════════════════════════
// TICK POSTURE AND EFFECTS (main entry point, called from AFlecsCharacter::Tick)
// ═══════════════════════════════════════════════════════════════════════════

void UFatumMovementComponent::TickPostureAndEffects(float DeltaTime,
	bool bSliding, bool bMantling, bool bHanging, uint8 MantleType, bool bClimbing)
{
	if (!MovementProfile) return;

	// Store sim-thread ability state for query functions
	bool bPrevHanging = bSimHanging;
	bSimSliding = bSliding;
	bSimMantling = bMantling;
	bSimHanging = bHanging;

	ACharacter* Char = GetCharacterOwner();
	if (!Char) return;

	const bool bAbilityOwnsPosture = bSliding || bMantling || bClimbing;

	// ── Slide capsule management ──
	if (bSliding && !bPrevSliding)
	{
		// Slide just activated → shrink capsule
		AbilityCapsuleHelper::ShrinkToCrouch(Char, this, MovementProfile);
		PostureSM.TargetEyeHeight = MovementProfile->SlideEyeHeight;
	}
	else if (!bSliding && bPrevSliding)
	{
		// Slide ended → restore capsule
		AbilityCapsuleHelper::RestoreFromCrouch(Char, this, MovementProfile);
	}

	// ── Mantle capsule management ──
	if (bMantling && !bPrevMantling)
	{
		// Mantle just activated → shrink capsule
		AbilityCapsuleHelper::ShrinkToCrouch(Char, this, MovementProfile);
		SetFeetToActorOffset(MovementProfile->CrouchHalfHeight);

		// Set eye height target based on mantle type
		if (MantleType == 2) // LedgeGrab
		{
			float HandToFeet = MovementProfile->StandingHalfHeight * 2.f - 25.f;
			PostureSM.TargetEyeHeight = HandToFeet - MovementProfile->LedgeHangCameraBelowLedge;
		}
		else // Vault / Mantle
		{
			PostureSM.TargetEyeHeight = MovementProfile->MantleEyeHeight;
		}
	}
	else if (!bMantling && bPrevMantling)
	{
		// Mantle ended → restore capsule
		AbilityCapsuleHelper::RestoreFromCrouch(Char, this, MovementProfile);
		float HH = Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		SetFeetToActorOffset(HH);
	}

	// Hang → Pull-up eye height transition
	// If mantling and just left hang state (pull-up started), switch to mantle eye height
	if (bMantling && bPrevMantling && !bHanging && bPrevHanging)
	{
		PostureSM.TargetEyeHeight = MovementProfile->MantleEyeHeight;
	}

	// ── Posture (skip while ability owns capsule) ──
	if (!bAbilityOwnsPosture)
	{
		UpdatePosture(DeltaTime);
	}

	// ── Eye height interpolation for active abilities ──
	if (bSliding)
	{
		PostureSM.CurrentEyeHeight = FMath::FInterpTo(
			PostureSM.CurrentEyeHeight, PostureSM.TargetEyeHeight,
			DeltaTime, MovementProfile->SlideTransitionSpeed);
	}
	else if (bMantling)
	{
		PostureSM.CurrentEyeHeight = FMath::FInterpTo(
			PostureSM.CurrentEyeHeight, PostureSM.TargetEyeHeight,
			DeltaTime, MovementProfile->MantleTransitionSpeed);
	}

	// ── Movement mode + camera effects ──
	UpdateMovementLayer(DeltaTime);
	UpdateCameraEffects(DeltaTime);

	bPrevSliding = bSliding;
	bPrevMantling = bMantling;
}

// ═══════════════════════════════════════════════════════════════════════════
// POSTURE
// ═══════════════════════════════════════════════════════════════════════════

void UFatumMovementComponent::UpdatePosture(float DeltaTime)
{
	if (!MovementProfile) return;

	bool bCanStandUp = CanExpandToHeight(MovementProfile->StandingHalfHeight);
	bool bCanCrouch = CanExpandToHeight(MovementProfile->CrouchHalfHeight);

	// Save pre-tick eye height for air camera stabilization
	float PreTickEyeHeight = PostureSM.CurrentEyeHeight;

	bool bChanged = PostureSM.Tick(DeltaTime, MovementProfile, bCanStandUp, bCanCrouch);

	if (bChanged)
	{
		float OldHH = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

		float R, HH;
		MovementProfile->GetCapsuleForPosture(PostureSM.CurrentPosture, R, HH);
		CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(R, HH, true);

		float HeightDelta = HH - OldHH;
		if (!FMath::IsNearlyZero(HeightDelta) && IsMovingOnGround())
		{
			PostureSM.CurrentEyeHeight -= HeightDelta;
		}

		OnPostureChanged.Broadcast(PostureSM.CurrentPosture);
	}

	if (!IsMovingOnGround())
	{
		// In air: freeze eye height to prevent camera bounce.
		PostureSM.CurrentEyeHeight = PreTickEyeHeight;
		PostureSM.TargetEyeHeight = PreTickEyeHeight;
	}
	else
	{
		// On ground: ensure target eye height matches current posture.
		float ExpectedTarget = MovementProfile->GetEyeHeightForPosture(PostureSM.CurrentPosture);
		if (!FMath::IsNearlyEqual(PostureSM.TargetEyeHeight, ExpectedTarget, 0.1f))
		{
			PostureSM.TargetEyeHeight = ExpectedTarget;
		}
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// MOVEMENT LAYER
// ═══════════════════════════════════════════════════════════════════════════

void UFatumMovementComponent::UpdateMovementLayer(float DeltaTime)
{
	const bool bGrounded = IsMovingOnGround();

	// Landing detection (for camera compress)
	if (!bWasGroundedLastFrame && bGrounded)
	{
		if (MovementProfile && LandingFallSpeed >= MovementProfile->LandingMinFallSpeed)
		{
			LandingCompressTimer = MovementProfile->LandingCameraCompressDuration;
			float CompressScale = FMath::Clamp(LandingFallSpeed / 1000.f, 0.f, 1.f);
			LandingCompressInitial = -MovementProfile->LandingCameraCompressAmount * CompressScale;
			LandingCompressOffset = LandingCompressInitial;
		}
		LandingFallSpeed = 0.f;
	}

	// Track maximum downward velocity during fall
	if (!bGrounded && Velocity.Z < 0.f)
	{
		LandingFallSpeed = FMath::Max(LandingFallSpeed, FMath::Abs(Velocity.Z));
	}

	// Determine movement mode from ability state + locomotion
	if (bSimMantling)
	{
		if (bSimHanging)
			TransitionMoveMode(ECharacterMoveMode::LedgeHang);
		else
			TransitionMoveMode(ECharacterMoveMode::Mantle);
	}
	else if (bSimSliding)
	{
		TransitionMoveMode(ECharacterMoveMode::Slide);
	}
	else if (bGrounded)
	{
		const float HorizSpeed = Velocity.Size2D();
		if (bWantsToSprint && HorizSpeed > 10.f && PostureSM.CurrentPosture == ECharacterPosture::Standing)
		{
			TransitionMoveMode(ECharacterMoveMode::Sprint);
		}
		else if (HorizSpeed > 10.f)
		{
			TransitionMoveMode(ECharacterMoveMode::Walk);
		}
		else
		{
			TransitionMoveMode(ECharacterMoveMode::Idle);
		}
	}
	else
	{
		if (Velocity.Z > 0.f)
		{
			TransitionMoveMode(ECharacterMoveMode::Jump);
		}
		else
		{
			TransitionMoveMode(ECharacterMoveMode::Fall);
		}
	}

	bWasGroundedLastFrame = bGrounded;
}

// ═══════════════════════════════════════════════════════════════════════════
// CAMERA EFFECTS
// ═══════════════════════════════════════════════════════════════════════════

void UFatumMovementComponent::UpdateCameraEffects(float DeltaTime)
{
	if (!MovementProfile) return;

	// FOV: sprint boost + slide inherits sprint FOV
	TargetFOVOffset = 0.f;
	if (IsSprinting() || bSimSliding)
	{
		TargetFOVOffset = MovementProfile->SprintFOVBoost;
	}
	CurrentFOVOffset = FMath::FInterpTo(CurrentFOVOffset, TargetFOVOffset,
		DeltaTime, MovementProfile->FOVInterpSpeed);

	// Head bob + slide tilt
	UpdateHeadBob(DeltaTime);
	UpdateSlideTilt(DeltaTime);

	// Landing compress spring-back
	if (LandingCompressTimer > 0.f)
	{
		LandingCompressTimer -= DeltaTime;
		if (LandingCompressTimer <= 0.f)
		{
			LandingCompressTimer = 0.f;
			LandingCompressOffset = 0.f;
		}
		else
		{
			float Alpha = LandingCompressTimer / MovementProfile->LandingCameraCompressDuration;
			LandingCompressOffset = LandingCompressInitial * Alpha;
		}
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// HEAD BOB
// ═══════════════════════════════════════════════════════════════════════════

void UFatumMovementComponent::UpdateHeadBob(float DeltaTime)
{
	float TargetScale = 0.f;
	bool bOnGround = IsMovingOnGround();
	bool bHasActiveAbility = bSimSliding || bSimMantling;
	float HorizSpeed = BarrageVelocity.Size2D();

	if (bOnGround && !bHasActiveAbility && HorizSpeed > 10.f
		&& PostureSM.CurrentPosture != ECharacterPosture::Prone)
	{
		TargetScale = 1.f;
		if (CurrentMoveMode == ECharacterMoveMode::Sprint)
		{
			TargetScale = MovementProfile->BobSprintMultiplier;
		}
		else if (PostureSM.CurrentPosture == ECharacterPosture::Crouching)
		{
			TargetScale = MovementProfile->BobCrouchMultiplier;
		}
	}

	HeadBobAmplitudeScale = FMath::FInterpTo(
		HeadBobAmplitudeScale, TargetScale, DeltaTime, MovementProfile->BobInterpSpeed);

	if (HeadBobAmplitudeScale > 0.001f)
	{
		HeadBobTimer += DeltaTime;
	}

	float VertFreq = FMath::Max(MovementProfile->BobVerticalFrequency, 0.01f);

	float Period = 2.f / VertFreq;
	HeadBobTimer = FMath::Fmod(HeadBobTimer, Period);
	float HorizFreq = VertFreq * 0.5f;

	HeadBobVerticalOffset = FMath::Sin(HeadBobTimer * VertFreq * UE_TWO_PI)
		* MovementProfile->BobVerticalAmplitude * HeadBobAmplitudeScale;
	HeadBobHorizontalOffset = FMath::Sin(HeadBobTimer * HorizFreq * UE_TWO_PI)
		* MovementProfile->BobHorizontalAmplitude * HeadBobAmplitudeScale;

	HeadBobVerticalOffset = FMath::Clamp(HeadBobVerticalOffset, -15.f, 15.f);
	HeadBobHorizontalOffset = FMath::Clamp(HeadBobHorizontalOffset, -10.f, 10.f);
}

// ═══════════════════════════════════════════════════════════════════════════
// SLIDE TILT
// ═══════════════════════════════════════════════════════════════════════════

void UFatumMovementComponent::UpdateSlideTilt(float DeltaTime)
{
	float TargetTilt = 0.f;

	if (bSimSliding && CharacterOwner)
	{
		FVector Right = CharacterOwner->GetActorRightVector();
		float LateralSpeed = FVector::DotProduct(BarrageVelocity, Right);

		float NormLateral = FMath::Clamp(LateralSpeed / FMath::Max(MovementProfile->SprintSpeed, 1.f), -1.f, 1.f);
		TargetTilt = NormLateral * MovementProfile->SlideCameraTiltAngle;
	}

	SlideTiltCurrent = FMath::FInterpTo(
		SlideTiltCurrent, TargetTilt, DeltaTime, MovementProfile->SlideTiltInterpSpeed);
}

void UFatumMovementComponent::TransitionMoveMode(ECharacterMoveMode NewMode)
{
	if (CurrentMoveMode == NewMode) return;
	CurrentMoveMode = NewMode;
}

// ═══════════════════════════════════════════════════════════════════════════
// COMMANDS
// ═══════════════════════════════════════════════════════════════════════════

void UFatumMovementComponent::RequestSprint(bool bSprint)
{
	bWantsToSprint = bSprint;
}

void UFatumMovementComponent::RequestCrouch(bool bPressed)
{
	if (!MovementProfile) return;

	// Skip crouch routing while ability owns capsule (sim thread handles it)
	if (bSimSliding || bSimMantling) return;

	PostureSM.RequestCrouch(bPressed, MovementProfile);
}

void UFatumMovementComponent::RequestProne(bool bPressed)
{
	if (!MovementProfile) return;
	if (bSimSliding || bSimMantling) return;

	PostureSM.RequestProne(bPressed, MovementProfile);
}

// ═══════════════════════════════════════════════════════════════════════════
// CEILING CHECK
// ═══════════════════════════════════════════════════════════════════════════

bool UFatumMovementComponent::CanExpandToHeight(float TargetHalfHeight) const
{
	if (!CharacterOwner) return false;

	float CurrentHH = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (TargetHalfHeight <= CurrentHH) return true;

	float HeightDiff = TargetHalfHeight - CurrentHH;
	float Radius = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius();

	FVector TestCenter = CharacterOwner->GetActorLocation() + FVector(0.f, 0.f, HeightDiff);

	FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, TargetHalfHeight);
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(CharacterOwner);

	return !GetWorld()->OverlapBlockingTestByChannel(TestCenter, FQuat::Identity, ECC_Pawn, Shape, Params);
}

void UFatumMovementComponent::BroadcastPostureChanged(ECharacterPosture Posture)
{
	OnPostureChanged.Broadcast(Posture);
}

// ═══════════════════════════════════════════════════════════════════════════
// CHARACTER ACCESSORS (delegate to AFlecsCharacter)
// ═══════════════════════════════════════════════════════════════════════════

void UFatumMovementComponent::SetFeetToActorOffset(float Value)
{
	AFlecsCharacter* FC = Cast<AFlecsCharacter>(GetCharacterOwner());
	if (FC) FC->SetFeetToActorOffset(Value);
}

FSkeletonKey UFatumMovementComponent::GetCharacterEntityKey() const
{
	const AFlecsCharacter* FC = Cast<AFlecsCharacter>(GetCharacterOwner());
	return FC ? FC->GetEntityKey() : FSkeletonKey();
}

TSharedPtr<FBarragePrimitive> UFatumMovementComponent::GetCharacterBarrageBody() const
{
	const AFlecsCharacter* FC = Cast<AFlecsCharacter>(GetCharacterOwner());
	return FC ? FC->GetCachedBarrageBody() : nullptr;
}

UFlecsArtillerySubsystem* UFatumMovementComponent::GetFlecsSubsystem() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UFlecsArtillerySubsystem>() : nullptr;
}
