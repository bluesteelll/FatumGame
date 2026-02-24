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
	UpdateMovementLayer(DeltaTime);
}

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
	if (bGrounded)
	{
		const float HorizSpeed = Velocity.Size2D();
		if (bWantsToSprint && HorizSpeed > 10.f && CurrentPosture == ECharacterPosture::Standing)
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

void UFatumMovementComponent::UpdateCameraEffects(float DeltaTime)
{
	if (!MovementProfile) return;

	// Sprint FOV
	TargetFOVOffset = IsSprinting() ? MovementProfile->SprintFOVBoost : 0.f;
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

float UFatumMovementComponent::GetMaxSpeed() const
{
	if (!MovementProfile) return Super::GetMaxSpeed();

	switch (CurrentMoveMode)
	{
	case ECharacterMoveMode::Sprint:
		return MovementProfile->SprintSpeed;
	default:
		break;
	}

	switch (CurrentPosture)
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
	return MovementProfile->GroundDeceleration;
}

void UFatumMovementComponent::RequestSprint(bool bSprint)
{
	bWantsToSprint = bSprint;
}

void UFatumMovementComponent::RequestJump()
{
	check(CharacterOwner);

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
	if (CharacterOwner)
	{
		CharacterOwner->StopJumping();
	}
}
