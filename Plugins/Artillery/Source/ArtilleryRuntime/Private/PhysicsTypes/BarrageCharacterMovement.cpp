
#include "PhysicsTypes/BarrageCharacterMovement.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "BarrageDispatch.h"
#include "KeyCarry.h"

UBarrageCharacterMovement::UBarrageCharacterMovement(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UBarrageCharacterMovement::BeginPlay()
{
	// IMPORTANT: Initialize capsule dimensions BEFORE calling Super::BeginPlay()
	// because Super::BeginPlay() calls RegistrationImplementation() which creates the physics body
	if (AActor* Owner = GetOwner())
	{
		// Try to get CapsuleComponent from Character
		if (ACharacter* Character = Cast<ACharacter>(Owner))
		{
			if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			{
				radius = Capsule->GetUnscaledCapsuleRadius();
				extent = Capsule->GetUnscaledCapsuleHalfHeight();

				UE_LOG(LogTemp, Log, TEXT("BarrageCharacterMovement: Initialized from CapsuleComponent - radius=%.1f, extent=%.1f"), radius, extent);
			}
		}
		else
		{
			// Fallback: try to find any CapsuleComponent on the owner
			if (UCapsuleComponent* Capsule = Owner->FindComponentByClass<UCapsuleComponent>())
			{
				radius = Capsule->GetUnscaledCapsuleRadius();
				extent = Capsule->GetUnscaledCapsuleHalfHeight();

				UE_LOG(LogTemp, Log, TEXT("BarrageCharacterMovement: Initialized from found CapsuleComponent - radius=%.1f, extent=%.1f"), radius, extent);
			}
			else
			{
				// Use reasonable defaults if no capsule found
				radius = 42.0;  // Default UE Character capsule radius
				extent = 96.0;  // Default UE Character capsule half-height

				UE_LOG(LogTemp, Warning, TEXT("BarrageCharacterMovement: No CapsuleComponent found, using defaults - radius=%.1f, extent=%.1f"), radius, extent);
			}
		}
	}

	// Now call parent BeginPlay (but NOT its RegistrationImplementation - we override that)
	// Note: UBarragePlayerAgent::BeginPlay calls RegistrationImplementation, which we override below
	Super::BeginPlay();
}

bool UBarrageCharacterMovement::RegistrationImplementation()
{
	if (MyParentObjectKey == 0 && GetOwner())
	{
		if (GetOwner()->GetComponentByClass<UKeyCarry>())
		{
			MyParentObjectKey = GetOwner()->GetComponentByClass<UKeyCarry>()->GetMyKey();
		}

		if (MyParentObjectKey == 0)
		{
			MyParentObjectKey = MAKE_ACTORKEY(GetOwner());
			ThrottleModel = FQuat4d(1, 1, 1, 1);
		}
	}

	if (!IsReady && MyParentObjectKey != 0 && !GetOwner()->GetActorLocation().ContainsNaN())
	{
		// IMPORTANT: Unreal Actor location is at CAPSULE CENTER
		// But Jolt expects the character position at the FEET (bottom of capsule)
		// We need to offset DOWN by the capsule half-height (extent)
		FVector ActorLocation = GetOwner()->GetActorLocation();
		FVector FeetLocation = FVector(ActorLocation.X, ActorLocation.Y, ActorLocation.Z - extent);

		UE_LOG(LogTemp, Log, TEXT("BarrageCharacterMovement: Creating physics body at feet position (%.1f, %.1f, %.1f), extent=%.1f"),
			FeetLocation.X, FeetLocation.Y, FeetLocation.Z, extent);

		FBCharParams params = FBarrageBounder::GenerateCharacterBounds(FeetLocation, radius, extent, HardMaxVelocity);
		MyBarrageBody = GetWorld()->GetSubsystem<UBarrageDispatch>()->CreatePrimitive(params, MyParentObjectKey, Layers::MOVING);

		if (MyBarrageBody && MyBarrageBody->tombstone == 0 && MyBarrageBody->Me != FBShape::Uninitialized)
		{
			IsReady = true;

			// CRITICAL: Initialize throttle model to override the default (100,100,100,100)
			// Parameters: (carryover, gravity, locomotion, forces)
			// - Carryover < 1.0 provides natural velocity decay (friction)
			// - 0.85 means 85% of velocity is retained each physics tick
			SetThrottleModel(0.85, 1.0, 1.0, 1.0);

			UE_LOG(LogTemp, Log, TEXT("BarrageCharacterMovement: Physics body created successfully!"));
			return true;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("BarrageCharacterMovement: Failed to create physics body"));
		}
	}
	return false;
}

void UBarrageCharacterMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// Call parent tick first (handles Barrage registration, etc)
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Cache pawn reference
	if (!CachedPawnOwner && GetOwner())
	{
		CachedPawnOwner = Cast<APawn>(GetOwner());
	}

	// Only process if Barrage body is ready
	if (!IsReady || !MyBarrageBody)
	{
		// Debug: log that we're waiting for physics body
		static bool bLoggedWaiting = false;
		if (!bLoggedWaiting)
		{
			UE_LOG(LogTemp, Warning, TEXT("BarrageCharacterMovement: Waiting for physics body... IsReady=%d, MyBarrageBody=%d"),
				IsReady ? 1 : 0, MyBarrageBody ? 1 : 0);
			bLoggedWaiting = true;
		}
		return;
	}

	// Update ground state
	FBarragePrimitive::FBGroundState CurrentGroundState = GetGroundState();
	bIsOnGround = (CurrentGroundState == FBarragePrimitive::FBGroundState::OnGround);

	// Debug: log ground state periodically
	static int DebugCounter = 0;
	if (++DebugCounter % 60 == 0)
	{
		FVector3f Pos = FBarragePrimitive::GetPosition(MyBarrageBody);
		FVector3f Vel = GetVelocity();
		UE_LOG(LogTemp, Log, TEXT("BarrageCharacterMovement: GroundState=%d, OnGround=%d, Pos=(%.1f,%.1f,%.1f), Vel=(%.1f,%.1f,%.1f), radius=%.1f, extent=%.1f"),
			(int)CurrentGroundState, bIsOnGround ? 1 : 0,
			Pos.X, Pos.Y, Pos.Z,
			Vel.X, Vel.Y, Vel.Z,
			radius, extent);
	}

	// Auto-process input if enabled
	if (bAutoProcessInput && CachedPawnOwner)
	{
		ProcessAutoInput();
	}

	// Auto-sync position if enabled
	if (bAutoSyncPosition)
	{
		SyncPositionWithPhysics();
	}
}

void UBarrageCharacterMovement::ProcessAutoInput()
{
	if (!CachedPawnOwner)
	{
		return;
	}

	// Get player controller
	APlayerController* PC = Cast<APlayerController>(CachedPawnOwner->GetController());
	if (!PC)
	{
		return;
	}

	// Process movement
	ProcessMovementInput();

	// Process jump
	ProcessJumpInput();
}

void UBarrageCharacterMovement::ProcessMovementInput()
{
	if (!CachedPawnOwner)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(CachedPawnOwner->GetController());
	if (!PC)
	{
		return;
	}

	// Get input values - use raw key states directly to avoid axis binding warnings
	// This works with both legacy input and Enhanced Input (since we check keys directly)
	bool bWPressed = PC->IsInputKeyDown(EKeys::W);
	bool bSPressed = PC->IsInputKeyDown(EKeys::S);
	bool bAPressed = PC->IsInputKeyDown(EKeys::A);
	bool bDPressed = PC->IsInputKeyDown(EKeys::D);

	float MoveForwardValue = (bWPressed ? 1.0f : 0.0f) + (bSPressed ? -1.0f : 0.0f);
	float MoveRightValue = (bDPressed ? 1.0f : 0.0f) + (bAPressed ? -1.0f : 0.0f);

	// Check if sprinting
	bIsSprinting = bEnableSprint && PC->IsInputKeyDown(EKeys::LeftShift);

	// Calculate movement speed
	float CurrentSpeed = MovementSpeed;
	if (bIsSprinting)
	{
		CurrentSpeed *= SprintSpeedMultiplier;
	}

	// Apply air control reduction
	if (!bIsOnGround)
	{
		CurrentSpeed *= AirControlMultiplier;
	}

	// Get control rotation
	FRotator ControlRotation = PC->GetControlRotation();

	// Calculate forward direction (ignore pitch)
	FRotator YawRotation(0, ControlRotation.Yaw, 0);
	FVector ForwardDir = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	FVector RightDir = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	// Calculate movement vector
	FVector MovementVector = (ForwardDir * MoveForwardValue + RightDir * MoveRightValue);

	// Normalize if length > 1 (diagonal movement)
	if (MovementVector.SizeSquared() > 1.0f)
	{
		MovementVector.Normalize();
	}

	// Scale by speed
	MovementVector *= CurrentSpeed;

	// Apply movement force
	if (!MovementVector.IsNearlyZero())
	{
		AddOneTickOfForce(FVector3f(MovementVector));
	}
	// Deceleration is handled by throttle model carryover (0.85 = 15% velocity decay per tick)
}

void UBarrageCharacterMovement::ProcessJumpInput()
{
	if (!CachedPawnOwner)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(CachedPawnOwner->GetController());
	if (!PC)
	{
		return;
	}

	// Check jump input
	bool bJumpPressed = PC->IsInputKeyDown(EKeys::SpaceBar);

	// Jump on press (not hold)
	if (bJumpPressed && !bWasJumpPressed && bIsOnGround)
	{
		Jump();
	}

	bWasJumpPressed = bJumpPressed;
}

void UBarrageCharacterMovement::SyncPositionWithPhysics()
{
	if (!IsReady || !MyBarrageBody || !GetOwner())
	{
		return;
	}

	// Get position from Barrage (this is at the CHARACTER FEET in Jolt)
	FVector3f BarragePos = FBarragePrimitive::GetPosition(MyBarrageBody);

	// Check for valid position (GetPosition returns NAN on failure)
	if (BarragePos.ContainsNaN())
	{
		return;
	}

	// IMPORTANT: Jolt character position is at the FEET (bottom of capsule)
	// But Unreal Actor location is at the CENTER of the capsule
	// We need to offset by the capsule half-height to match Unreal's convention
	FVector NewLocation(BarragePos.X, BarragePos.Y, BarragePos.Z + extent);

	GetOwner()->SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);
}

void UBarrageCharacterMovement::AddMovementInput(FVector WorldDirection, float ScaleValue)
{
	if (!IsReady || !MyBarrageBody)
	{
		return;
	}

	FVector Force = WorldDirection.GetSafeNormal() * MovementSpeed * ScaleValue;
	AddOneTickOfForce(FVector3f(Force));
}

void UBarrageCharacterMovement::Jump()
{
	if (!IsReady || !MyBarrageBody)
	{
		return;
	}

	// Only jump if on ground
	if (bIsOnGround)
	{
		FVector3f JumpForce(0, 0, JumpImpulse);
		UE_LOG(LogTemp, Log, TEXT("BarrageCharacterMovement: JUMP! Force=(0, 0, %.1f), GroundState=%d"),
			JumpImpulse, (int)GetGroundState());
		AddOneTickOfForce(JumpForce);
		bWantsToJump = false;
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("BarrageCharacterMovement: Jump blocked - not on ground (GroundState=%d)"),
			(int)GetGroundState());
	}
}

void UBarrageCharacterMovement::StopJumping()
{
	bWantsToJump = false;
}
