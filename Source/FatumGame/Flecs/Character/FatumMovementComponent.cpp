#include "FatumMovementComponent.h"
#include "FlecsMovementProfile.h"
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

void UFatumMovementComponent::ApplyProfile()
{
	if (!MovementProfile)
	{
		UE_LOG(LogTemp, Warning, TEXT("UFatumMovementComponent::ApplyProfile: No MovementProfile set! Using CMC defaults."));
		return;
	}

	MaxWalkSpeed = MovementProfile->WalkSpeed;
	MaxWalkSpeedCrouched = MovementProfile->CrouchSpeed;
	JumpZVelocity = MovementProfile->JumpVelocity;
	GravityScale = MovementProfile->GravityScale;
	AirControl = MovementProfile->AirControlMultiplier;
	BrakingDecelerationWalking = MovementProfile->GroundDeceleration;
	MaxAcceleration = MovementProfile->GroundAcceleration;

	PostureSM.CurrentEyeHeight = MovementProfile->StandingEyeHeight;
	PostureSM.TargetEyeHeight = MovementProfile->StandingEyeHeight;
}

void UFatumMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickHSM(DeltaTime);
	UpdateCameraEffects(DeltaTime);
}

void UFatumMovementComponent::TickHSM(float DeltaTime)
{
	if (bIsSliding)
	{
		// Slide owns capsule and eye height — skip PostureSM to avoid conflicts
		UpdateSlide(DeltaTime);
		UpdateMovementLayer(DeltaTime);
		return;
	}

	UpdatePosture(DeltaTime);
	UpdateMovementLayer(DeltaTime);
}

// ═══════════════════════════════════════════════════════════════════════════
// POSTURE
// ═══════════════════════════════════════════════════════════════════════════

void UFatumMovementComponent::UpdatePosture(float DeltaTime)
{
	if (!MovementProfile) return;

	bool bCanStandUp = CheckCanExpandTo(MovementProfile->StandingHalfHeight);
	bool bCanCrouch = CheckCanExpandTo(MovementProfile->CrouchHalfHeight);

	bool bChanged = PostureSM.Tick(DeltaTime, MovementProfile, bCanStandUp, bCanCrouch);

	if (bChanged)
	{
		float OldHH = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

		float R, HH;
		MovementProfile->GetCapsuleForPosture(PostureSM.CurrentPosture, R, HH);
		CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(R, HH, true);

		// Adjust actor position so feet stay on the ground.
		// Capsule grows/shrinks from center — without this, expanding clips into floor
		// and CMC resolves it next tick → jitter.
		float HeightDelta = HH - OldHH;
		if (!FMath::IsNearlyZero(HeightDelta))
		{
			FVector Loc = CharacterOwner->GetActorLocation();
			Loc.Z += HeightDelta;
			CharacterOwner->SetActorLocation(Loc);

			// Compensate eye height so camera stays at the same world-space position.
			// Actor shifted by HeightDelta → offset CurrentEyeHeight by -HeightDelta.
			// The smooth interpolation then transitions to TargetEyeHeight naturally.
			PostureSM.CurrentEyeHeight -= HeightDelta;
		}

		OnPostureChanged.Broadcast(PostureSM.CurrentPosture);
	}
}

bool UFatumMovementComponent::CheckCanExpandTo(float TargetHalfHeight) const
{
	if (!CharacterOwner) return false;

	float CurrentHH = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (TargetHalfHeight <= CurrentHH) return true; // shrinking is always ok

	float HeightDiff = TargetHalfHeight - CurrentHH;
	float Radius = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius();

	// Test at the position where capsule center would be after expansion (feet stay on ground).
	// Feet = ActorZ - CurrentHH, new center = Feet + TargetHH = ActorZ + HeightDiff.
	// Without this offset, the taller capsule clips through the floor → always blocked.
	FVector TestCenter = CharacterOwner->GetActorLocation() + FVector(0.f, 0.f, HeightDiff);

	FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, TargetHalfHeight);
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(CharacterOwner);

	return !GetWorld()->OverlapBlockingTestByChannel(TestCenter, FQuat::Identity, ECC_Pawn, Shape, Params);
}

// ═══════════════════════════════════════════════════════════════════════════
// SLIDE
// ═══════════════════════════════════════════════════════════════════════════

void UFatumMovementComponent::BeginSlide()
{
	check(CharacterOwner && MovementProfile);

	bIsSliding = true;
	SlideTimer = MovementProfile->SlideMaxDuration;
	SlideCurrentSpeed = Velocity.Size2D() + MovementProfile->SlideInitialSpeedBoost;
	bSlideCrouchHeld = true;

	// Suppress CMC's ground friction during slide (it multiplies BrakingDeceleration)
	GroundFriction = MovementProfile->SlideGroundFriction;

	// Shrink capsule to crouch dimensions (same pattern as UpdatePosture)
	float OldHH = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(
		MovementProfile->CrouchRadius, MovementProfile->CrouchHalfHeight, true);

	float HeightDelta = MovementProfile->CrouchHalfHeight - OldHH;
	if (!FMath::IsNearlyZero(HeightDelta))
	{
		FVector Loc = CharacterOwner->GetActorLocation();
		Loc.Z += HeightDelta;
		CharacterOwner->SetActorLocation(Loc);

		PostureSM.CurrentEyeHeight -= HeightDelta;
	}

	// Set posture to Crouching so external queries (ECS sync, HUD) see correct state
	PostureSM.CurrentPosture = ECharacterPosture::Crouching;
	PostureSM.TargetEyeHeight = MovementProfile->SlideEyeHeight;

	// Sync Barrage capsule to crouch dimensions
	OnPostureChanged.Broadcast(ECharacterPosture::Crouching);
}

void UFatumMovementComponent::EndSlide()
{
	check(CharacterOwner && MovementProfile);

	bIsSliding = false;
	SlideTimer = 0.f;
	SlideCurrentSpeed = 0.f;

	// Restore CMC ground friction
	GroundFriction = 8.f; // CMC default

	// Determine target posture after slide
	ECharacterPosture TargetPosture;
	if (bSlideCrouchHeld)
	{
		TargetPosture = ECharacterPosture::Crouching;
	}
	else if (CheckCanExpandTo(MovementProfile->StandingHalfHeight))
	{
		TargetPosture = ECharacterPosture::Standing;
	}
	else
	{
		TargetPosture = ECharacterPosture::Crouching;
	}

	float OldHH = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	float R, HH;
	MovementProfile->GetCapsuleForPosture(TargetPosture, R, HH);
	CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(R, HH, true);

	float HeightDelta = HH - OldHH;
	if (!FMath::IsNearlyZero(HeightDelta))
	{
		FVector Loc = CharacterOwner->GetActorLocation();
		Loc.Z += HeightDelta;
		CharacterOwner->SetActorLocation(Loc);

		PostureSM.CurrentEyeHeight -= HeightDelta;
	}

	// Sync PostureSM state to match the resulting posture
	PostureSM.CurrentPosture = TargetPosture;
	PostureSM.TargetEyeHeight = MovementProfile->GetEyeHeightForPosture(TargetPosture);

	if (TargetPosture == ECharacterPosture::Crouching)
	{
		PostureSM.RequestCrouch(true, MovementProfile);
	}
	else
	{
		PostureSM.ForceClearCrouch();
	}

	OnPostureChanged.Broadcast(TargetPosture);
}

void UFatumMovementComponent::UpdateSlide(float DeltaTime)
{
	if (!bIsSliding) return;
	check(MovementProfile); // Was valid at BeginSlide, must still be valid

	// Decelerate
	SlideCurrentSpeed -= MovementProfile->SlideDeceleration * DeltaTime;
	SlideCurrentSpeed = FMath::Max(SlideCurrentSpeed, 0.f);

	// Interpolate eye height toward slide eye height
	PostureSM.CurrentEyeHeight = FMath::FInterpTo(
		PostureSM.CurrentEyeHeight, PostureSM.TargetEyeHeight,
		DeltaTime, MovementProfile->SlideTransitionSpeed);

	// Decrement timer
	SlideTimer -= DeltaTime;

	// Exit conditions
	if (SlideTimer <= 0.f
		|| SlideCurrentSpeed < MovementProfile->SlideMinExitSpeed
		|| !IsMovingOnGround())
	{
		EndSlide();
	}
}

void UFatumMovementComponent::RequestCrouch(bool bPressed)
{
	// During slide: track crouch hold state
	if (bIsSliding)
	{
		bSlideCrouchHeld = bPressed;
		if (!bPressed)
		{
			EndSlide();
		}
		return;
	}

	// Slide entry: sprint + grounded + fast enough
	if (bPressed && CurrentMoveMode == ECharacterMoveMode::Sprint
		&& IsMovingOnGround() && MovementProfile
		&& Velocity.Size2D() >= MovementProfile->SlideMinEntrySpeed)
	{
		BeginSlide();
		return;
	}

	PostureSM.RequestCrouch(bPressed, MovementProfile);
}

void UFatumMovementComponent::RequestProne(bool bPressed)
{
	if (bIsSliding) return; // No prone during slide

	PostureSM.RequestProne(bPressed, MovementProfile);
}

// ═══════════════════════════════════════════════════════════════════════════
// MOVEMENT LAYER
// ═══════════════════════════════════════════════════════════════════════════

void UFatumMovementComponent::UpdateMovementLayer(float DeltaTime)
{
	// Decrement timers
	if (CoyoteTimer > 0.f) CoyoteTimer -= DeltaTime;
	if (JumpBufferTimer > 0.f) JumpBufferTimer -= DeltaTime;

	const bool bGrounded = IsMovingOnGround();

	// Coyote time: just left ground without intentional jump
	if (bWasGroundedLastFrame && !bGrounded && !bJumpedIntentionally)
	{
		if (MovementProfile)
		{
			CoyoteTimer = MovementProfile->CoyoteTimeFrames * SimFrameTime;
		}
	}

	// Landing detection
	if (!bWasGroundedLastFrame && bGrounded)
	{
		// Trigger landing camera compress
		if (MovementProfile && LandingFallSpeed >= MovementProfile->LandingMinFallSpeed)
		{
			LandingCompressTimer = MovementProfile->LandingCameraCompressDuration;
			float CompressScale = FMath::Clamp(LandingFallSpeed / 1000.f, 0.f, 1.f);
			LandingCompressInitial = -MovementProfile->LandingCameraCompressAmount * CompressScale;
			LandingCompressOffset = LandingCompressInitial;
		}

		// Jump buffer: execute stored jump on landing
		if (JumpBufferTimer > 0.f)
		{
			JumpBufferTimer = 0.f;
			CharacterOwner->Jump();
		}

		bJumpedIntentionally = false;
		LandingFallSpeed = 0.f;
	}

	// Track maximum downward velocity during fall
	if (!bGrounded && Velocity.Z < 0.f)
	{
		LandingFallSpeed = FMath::Max(LandingFallSpeed, FMath::Abs(Velocity.Z));
	}

	// Determine movement mode
	if (bIsSliding)
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

	// Sprint / Slide FOV
	TargetFOVOffset = (IsSprinting() || bIsSliding) ? MovementProfile->SprintFOVBoost : 0.f;
	CurrentFOVOffset = FMath::FInterpTo(CurrentFOVOffset, TargetFOVOffset,
		DeltaTime, MovementProfile->FOVInterpSpeed);

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

void UFatumMovementComponent::TransitionMoveMode(ECharacterMoveMode NewMode)
{
	if (CurrentMoveMode == NewMode) return;
	CurrentMoveMode = NewMode;
}

// ═══════════════════════════════════════════════════════════════════════════
// SPEED OVERRIDES
// ═══════════════════════════════════════════════════════════════════════════

float UFatumMovementComponent::GetMaxSpeed() const
{
	if (!MovementProfile) return Super::GetMaxSpeed();

	switch (CurrentMoveMode)
	{
	case ECharacterMoveMode::Sprint:
		return MovementProfile->SprintSpeed;
	case ECharacterMoveMode::Slide:
		return SlideCurrentSpeed;
	default:
		break;
	}

	switch (PostureSM.CurrentPosture)
	{
	case ECharacterPosture::Crouching:
		return MovementProfile->CrouchSpeed;
	case ECharacterPosture::Prone:
		return MovementProfile->ProneSpeed;
	default:
		return MovementProfile->WalkSpeed;
	}
}

float UFatumMovementComponent::GetMaxAcceleration() const
{
	if (!MovementProfile) return Super::GetMaxAcceleration();

	if (bIsSliding) return 100.f; // Minimal — allows slight direction tweaking only

	if (IsFalling())
	{
		return MovementProfile->AirAcceleration;
	}
	if (IsSprinting())
	{
		return MovementProfile->SprintAcceleration;
	}
	return MovementProfile->GroundAcceleration;
}

float UFatumMovementComponent::GetMaxBrakingDeceleration() const
{
	if (!MovementProfile) return Super::GetMaxBrakingDeceleration();

	if (bIsSliding) return MovementProfile->SlideGroundFriction * 100.f;

	return MovementProfile->GroundDeceleration;
}

// ═══════════════════════════════════════════════════════════════════════════
// SPRINT / JUMP
// ═══════════════════════════════════════════════════════════════════════════

void UFatumMovementComponent::RequestSprint(bool bSprint)
{
	bWantsToSprint = bSprint;
}

void UFatumMovementComponent::RequestJump()
{
	check(CharacterOwner);

	// Slide-cancel jump: end slide, preserve horizontal momentum, jump
	if (bIsSliding && MovementProfile)
	{
		EndSlide();
		JumpZVelocity = MovementProfile->SlideJumpVelocity;
		CoyoteTimer = 0.f;
		JumpBufferTimer = 0.f;
		bJumpedIntentionally = true;
		CharacterOwner->Jump();
		return;
	}

	// No jump from prone
	if (PostureSM.CurrentPosture == ECharacterPosture::Prone) return;

	// Crouch jump: stand up + jump with reduced velocity
	if (PostureSM.CurrentPosture == ECharacterPosture::Crouching && MovementProfile)
	{
		PostureSM.ForceClearCrouch();
		JumpZVelocity = MovementProfile->CrouchJumpVelocity;
	}
	else if (MovementProfile)
	{
		JumpZVelocity = MovementProfile->JumpVelocity;
	}

	// Grounded or coyote time -> jump immediately
	if (IsMovingOnGround() || CoyoteTimer > 0.f)
	{
		CoyoteTimer = 0.f;
		JumpBufferTimer = 0.f;
		bJumpedIntentionally = true;
		CharacterOwner->Jump();
	}
	else
	{
		// Airborne -> buffer the jump
		if (MovementProfile)
		{
			JumpBufferTimer = MovementProfile->JumpBufferFrames * SimFrameTime;
		}
	}
}

void UFatumMovementComponent::ReleaseJump()
{
	check(CharacterOwner);
	CharacterOwner->StopJumping();
}
