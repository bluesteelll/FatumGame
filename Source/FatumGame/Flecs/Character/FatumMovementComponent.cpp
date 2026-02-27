#include "FatumMovementComponent.h"
#include "MovementAbility.h"
#include "SlideAbility.h"
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

	// Register movement abilities
	RegisterAbility(USlideAbility::StaticClass());
}

void UFatumMovementComponent::ApplyProfile()
{
	if (!MovementProfile)
	{
		UE_LOG(LogTemp, Warning, TEXT("UFatumMovementComponent::ApplyProfile: No MovementProfile set!"));
		return;
	}

	// CMC params are irrelevant (Barrage drives movement).
	// Only eye height needs initialization for camera.
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

void UFatumMovementComponent::TickPostureAndEffects(float DeltaTime)
{
	TickHSM(DeltaTime);
	UpdateCameraEffects(DeltaTime);
}

// ═══════════════════════════════════════════════════════════════════════════
// ABILITY SYSTEM
// ═══════════════════════════════════════════════════════════════════════════

void UFatumMovementComponent::RegisterAbility(TSubclassOf<UMovementAbility> AbilityClass)
{
	check(AbilityClass);
	UMovementAbility* Ability = NewObject<UMovementAbility>(this, AbilityClass);
	Ability->Initialize(this);
	Abilities.Add(Ability);
}

void UFatumMovementComponent::ActivateAbility(UMovementAbility* Ability)
{
	check(Ability);
	checkf(Abilities.Contains(Ability), TEXT("Ability %s not registered"), *Ability->GetName());

	if (ActiveAbility)
	{
		DeactivateAbility();
	}
	ActiveAbility = Ability;
	Ability->bActive = true;
	Ability->OnActivated();
}

void UFatumMovementComponent::DeactivateAbility()
{
	if (!ActiveAbility) return;

	// Deregister before callback — prevents reentrancy crash if OnDeactivated
	// triggers code that calls DeactivateAbility again.
	UMovementAbility* Ability = ActiveAbility;
	ActiveAbility = nullptr;
	Ability->bActive = false;
	Ability->OnDeactivated();
}

bool UFatumMovementComponent::IsSliding() const
{
	return ActiveAbility && ActiveAbility->GetMoveMode() == ECharacterMoveMode::Slide;
}

bool UFatumMovementComponent::CanExpandToHeight(float TargetHalfHeight) const
{
	if (!CharacterOwner) return false;

	float CurrentHH = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (TargetHalfHeight <= CurrentHH) return true; // shrinking is always ok

	float HeightDiff = TargetHalfHeight - CurrentHH;
	float Radius = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius();

	// Test at the position where capsule center would be after expansion (feet stay on ground).
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
// HSM
// ═══════════════════════════════════════════════════════════════════════════

void UFatumMovementComponent::TickHSM(float DeltaTime)
{
	// Active ability may own posture (e.g. Slide manages capsule directly)
	if (!ActiveAbility || !ActiveAbility->OwnsPosture())
	{
		UpdatePosture(DeltaTime);
	}

	if (ActiveAbility)
	{
		ActiveAbility->OnTick(DeltaTime);
	}

	UpdateMovementLayer(DeltaTime);
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
			// Barrage sync in AFlecsCharacter::Tick handles Z via FeetToActorOffset.
			// No SetActorLocation here — would cause double-move in one frame.
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

		// Jump buffer: execute stored jump on landing via Barrage OtherForce
		if (JumpBufferTimer > 0.f && MovementProfile)
		{
			JumpBufferTimer = 0.f;
			PendingJumpImpulse = MovementProfile->JumpVelocity;
		}

		bJumpedIntentionally = false;
		LandingFallSpeed = 0.f;
	}

	// Track maximum downward velocity during fall
	if (!bGrounded && Velocity.Z < 0.f)
	{
		LandingFallSpeed = FMath::Max(LandingFallSpeed, FMath::Abs(Velocity.Z));
	}

	// Determine movement mode — active ability takes priority
	if (ActiveAbility)
	{
		TransitionMoveMode(ActiveAbility->GetMoveMode());
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

	// FOV: sprint base + ability override (take larger)
	TargetFOVOffset = IsSprinting() ? MovementProfile->SprintFOVBoost : 0.f;
	if (ActiveAbility)
	{
		float AbilityFOV = ActiveAbility->GetTargetFOVOffset();
		if (AbilityFOV > TargetFOVOffset)
		{
			TargetFOVOffset = AbilityFOV;
		}
	}
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
// COMMANDS
// ═══════════════════════════════════════════════════════════════════════════

void UFatumMovementComponent::RequestSprint(bool bSprint)
{
	bWantsToSprint = bSprint;
}

void UFatumMovementComponent::RequestJump()
{
	check(CharacterOwner);

	// Let active ability handle jump (e.g. slide-cancel jump)
	if (ActiveAbility && ActiveAbility->HandleJumpRequest())
	{
		CoyoteTimer = 0.f;
		JumpBufferTimer = 0.f;
		bJumpedIntentionally = true;
		return;
	}

	// No jump from prone
	if (PostureSM.CurrentPosture == ECharacterPosture::Prone) return;

	// Determine jump velocity (cm/s) — crouch jump uses reduced velocity
	float JumpVel = 0.f;
	if (PostureSM.CurrentPosture == ECharacterPosture::Crouching && MovementProfile)
	{
		PostureSM.ForceClearCrouch();
		JumpVel = MovementProfile->CrouchJumpVelocity;
	}
	else if (MovementProfile)
	{
		JumpVel = MovementProfile->JumpVelocity;
	}

	// Grounded or coyote time -> jump immediately via Barrage OtherForce
	if (IsMovingOnGround() || CoyoteTimer > 0.f)
	{
		CoyoteTimer = 0.f;
		JumpBufferTimer = 0.f;
		bJumpedIntentionally = true;
		PendingJumpImpulse = JumpVel;
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
	// Barrage handles jump physics — nothing to do on release.
}

void UFatumMovementComponent::RequestCrouch(bool bPressed)
{
	check(MovementProfile);

	// Active slide ability intercepts crouch input
	if (auto* Slide = Cast<USlideAbility>(ActiveAbility))
	{
		Slide->OnCrouchInput(bPressed);
		return;
	}

	// Try slide activation: sprint + grounded + fast enough
	if (bPressed)
	{
		if (auto* Slide = FindAbility<USlideAbility>())
		{
			if (Slide->CanActivate())
			{
				ActivateAbility(Slide);
				return;
			}
		}
	}

	PostureSM.RequestCrouch(bPressed, MovementProfile);
}

void UFatumMovementComponent::RequestProne(bool bPressed)
{
	check(MovementProfile);
	if (ActiveAbility) return; // No prone during active ability

	PostureSM.RequestProne(bPressed, MovementProfile);
}
