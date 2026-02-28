#include "FatumMovementComponent.h"
#include "MovementAbility.h"
#include "SlideAbility.h"
#include "MantleAbility.h"
#include "FlecsCharacter.h"
#include "FlecsArtillerySubsystem.h"
#include "GameFramework/PlayerController.h"
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
	RegisterAbility(UMantleAbility::StaticClass());
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

	// Tick mantle cooldown even when ability is inactive
	if (UMantleAbility* MA = FindAbility<UMantleAbility>())
	{
		MA->TickCooldown(DeltaTime);
	}
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

bool UFatumMovementComponent::IsMantling() const
{
	if (!ActiveAbility) return false;
	ECharacterMoveMode Mode = ActiveAbility->GetMoveMode();
	return Mode == ECharacterMoveMode::Vault || Mode == ECharacterMoveMode::Mantle || Mode == ECharacterMoveMode::LedgeHang;
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

	// Airborne ledge detection at ~10Hz for ledge grab
	if (IsFalling() && !ActiveAbility)
	{
		AirborneDetectionTimer += DeltaTime;
		if (AirborneDetectionTimer >= 0.1f)
		{
			AirborneDetectionTimer -= 0.1f;
			if (UMantleAbility* MA = FindAbility<UMantleAbility>())
			{
				if (MA->GetCooldownRemaining() <= 0.f)
				{
					ACharacter* Char = GetCharacterOwner();
					if (Char)
					{
						APlayerController* PC = Cast<APlayerController>(Char->Controller);
						FVector LookDir = PC ? PC->GetControlRotation().Vector() : Char->GetActorForwardVector();
						float HH = Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
						FVector FeetPos = Char->GetActorLocation() - FVector(0, 0, HH);
						float Radius = Char->GetCapsuleComponent()->GetScaledCapsuleRadius();
						MA->PerformDetection(FeetPos, LookDir, Radius, HH);
					}
				}
			}
		}
	}
	else
	{
		AirborneDetectionTimer = 0.f;
	}
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
		// Invalidate airborne ledge detection cache on landing
		if (UMantleAbility* MA = FindAbility<UMantleAbility>())
		{
			MA->InvalidateCache();
		}

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
	// Determine target amplitude scale based on movement state
	float TargetScale = 0.f;
	bool bOnGround = IsMovingOnGround();
	bool bHasActiveAbility = ActiveAbility != nullptr;
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

	// Smooth fade in/out (never snap)
	HeadBobAmplitudeScale = FMath::FInterpTo(
		HeadBobAmplitudeScale, TargetScale, DeltaTime, MovementProfile->BobInterpSpeed);

	// Advance timer continuously (no reset on mode change)
	if (HeadBobAmplitudeScale > 0.001f)
	{
		HeadBobTimer += DeltaTime;
	}

	// Lissajous offsets: Vert = sin(2f*t), Horiz = sin(f*t)
	float VertFreq = FMath::Max(MovementProfile->BobVerticalFrequency, 0.01f);

	// Wrap timer to avoid float precision degradation in long sessions.
	// Common period = 2/VertFreq (horiz completes 1 cycle, vert completes 2).
	float Period = 2.f / VertFreq;
	HeadBobTimer = FMath::Fmod(HeadBobTimer, Period);
	float HorizFreq = VertFreq * 0.5f;

	HeadBobVerticalOffset = FMath::Sin(HeadBobTimer * VertFreq * UE_TWO_PI)
		* MovementProfile->BobVerticalAmplitude * HeadBobAmplitudeScale;
	HeadBobHorizontalOffset = FMath::Sin(HeadBobTimer * HorizFreq * UE_TWO_PI)
		* MovementProfile->BobHorizontalAmplitude * HeadBobAmplitudeScale;

	// Safety clamp (matches UPROPERTY ClampMax on profile)
	HeadBobVerticalOffset = FMath::Clamp(HeadBobVerticalOffset, -15.f, 15.f);
	HeadBobHorizontalOffset = FMath::Clamp(HeadBobHorizontalOffset, -10.f, 10.f);
}

// ═══════════════════════════════════════════════════════════════════════════
// SLIDE TILT
// ═══════════════════════════════════════════════════════════════════════════

void UFatumMovementComponent::UpdateSlideTilt(float DeltaTime)
{
	float TargetTilt = 0.f;

	if (IsSliding() && CharacterOwner)
	{
		// Project velocity onto actor right vector → lateral component
		FVector Right = CharacterOwner->GetActorRightVector();
		float LateralSpeed = FVector::DotProduct(BarrageVelocity, Right);

		// Normalize to [-1, 1] using sprint speed as reference
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

void UFatumMovementComponent::RequestJump()
{
	check(CharacterOwner);

	// Let active ability handle jump (e.g. slide-cancel jump, mantle exit)
	if (ActiveAbility && ActiveAbility->HandleJumpRequest())
	{
		CoyoteTimer = 0.f;
		JumpBufferTimer = 0.f;
		bJumpedIntentionally = true;
		return;
	}

	// No active ability — check mantle/vault/ledge grab BEFORE normal jump
	if (UMantleAbility* MantleAbil = FindAbility<UMantleAbility>())
	{
		// Grounded: run detection on-demand when jump is pressed
		if (IsMovingOnGround())
		{
			ACharacter* Char = GetCharacterOwner();
			APlayerController* PC = Char ? Cast<APlayerController>(Char->Controller) : nullptr;
			FVector LookDir = PC ? PC->GetControlRotation().Vector() : Char->GetActorForwardVector();
			float HH = Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			FVector FeetPos = Char->GetActorLocation() - FVector(0, 0, HH);
			float Radius = Char->GetCapsuleComponent()->GetScaledCapsuleRadius();
			MantleAbil->PerformDetection(FeetPos, LookDir, Radius, HH);
		}
		// Airborne: CachedCandidate already populated by 10Hz timer

		if (MantleAbil->CanActivate())
		{
			ActivateAbility(MantleAbil);
			CoyoteTimer = 0.f;
			JumpBufferTimer = 0.f;
			bJumpedIntentionally = true;
			return;
		}
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

	// Active ability intercepts crouch input (slide, mantle, etc.)
	if (ActiveAbility && ActiveAbility->HandleCrouchInput(bPressed))
	{
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
