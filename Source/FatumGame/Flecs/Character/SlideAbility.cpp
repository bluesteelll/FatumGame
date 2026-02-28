// Slide ability — thin game-thread wrapper.
// Physics (deceleration, timer, exit conditions) runs on sim thread via FSlideInstance
// in PrepareCharacterStep. This class handles visuals, capsule, and input routing.

#include "SlideAbility.h"
#include "AbilityCapsuleHelper.h"
#include "FatumMovementComponent.h"
#include "FlecsMovementProfile.h"
#include "FlecsMovementStatic.h"
#include "FPostureStateMachine.h"
#include "FlecsCharacter.h"
#include "FlecsArtillerySubsystem.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

bool USlideAbility::CanActivate() const
{
	check(OwnerCMC);
	if (!OwnerCMC->MovementProfile) return false;

	return OwnerCMC->GetCurrentMoveMode() == ECharacterMoveMode::Sprint
		&& OwnerCMC->IsMovingOnGround()
		&& OwnerCMC->GetBarrageVelocity().Size2D() >= OwnerCMC->MovementProfile->SlideMinEntrySpeed;
}

void USlideAbility::OnActivated()
{
	check(OwnerCMC && OwnerCMC->MovementProfile);

	ACharacter* Char = OwnerCMC->GetCharacterOwner();
	check(Char);

	const UFlecsMovementProfile* Profile = OwnerCMC->MovementProfile;

	bSlideCrouchHeld = true;

	// Tell sim thread: slide is now active (via EnqueueCommand → FSlideInstance)
	float InitSpeed = OwnerCMC->GetBarrageVelocity().Size2D() + Profile->SlideInitialSpeedBoost;
	float MaxDuration = Profile->SlideMaxDuration;

	AFlecsCharacter* FlecsChar = Cast<AFlecsCharacter>(Char);
	if (FlecsChar)
	{
		UFlecsArtillerySubsystem* Sub = FlecsChar->GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
		FSkeletonKey Key = FlecsChar->GetEntityKey();
		if (Sub && Key.IsValid())
		{
			Sub->EnqueueCommand([Sub, Key, InitSpeed, MaxDuration]()
			{
				flecs::entity Entity = Sub->GetEntityForBarrageKey(Key);
				if (Entity.is_valid())
				{
					FSlideInstance SI;
					SI.CurrentSpeed = InitSpeed;
					SI.Timer = MaxDuration;
					Entity.set<FSlideInstance>(SI);
				}
			});
		}
	}

	// Shrink capsule to crouch dimensions (shared helper)
	AbilityCapsuleHelper::ShrinkToCrouch(Char, OwnerCMC, Profile);

	// Override eye height target for slide (helper sets it to crouch default)
	OwnerCMC->GetPostureSM().TargetEyeHeight = Profile->SlideEyeHeight;
}

void USlideAbility::OnTick(float DeltaTime)
{
	check(OwnerCMC && OwnerCMC->MovementProfile);

	// Eye height interpolation only — physics runs on sim thread
	const UFlecsMovementProfile* Profile = OwnerCMC->MovementProfile;
	FPostureStateMachine& SM = OwnerCMC->GetPostureSM();
	SM.CurrentEyeHeight = FMath::FInterpTo(
		SM.CurrentEyeHeight, SM.TargetEyeHeight,
		DeltaTime, Profile->SlideTransitionSpeed);

	// Sim-thread exit detection happens in FlecsCharacter::Tick (reads bSlideActive atomic).
	// No speed/timer logic here — PrepareCharacterStep owns it.
}

void USlideAbility::OnDeactivated()
{
	check(OwnerCMC && OwnerCMC->MovementProfile);

	ACharacter* Char = OwnerCMC->GetCharacterOwner();
	check(Char);

	const UFlecsMovementProfile* Profile = OwnerCMC->MovementProfile;

	// Tell sim thread: slide ended (remove FSlideInstance)
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
					Entity.remove<FSlideInstance>();
				}
			});
		}
	}

	// Restore capsule to best-fit posture (shared helper)
	AbilityCapsuleHelper::RestoreFromCrouch(Char, OwnerCMC, Profile, !bSlideCrouchHeld);
}

// ═══════════════════════════════════════════════════════════════════════════
// CAMERA
// ═══════════════════════════════════════════════════════════════════════════

float USlideAbility::GetTargetFOVOffset() const
{
	check(OwnerCMC && OwnerCMC->MovementProfile);
	return OwnerCMC->MovementProfile->SprintFOVBoost;
}

// ═══════════════════════════════════════════════════════════════════════════
// INPUT INTERCEPTION
// ═══════════════════════════════════════════════════════════════════════════

bool USlideAbility::HandleJumpRequest()
{
	check(OwnerCMC && OwnerCMC->MovementProfile);

	// Slide-cancel jump: deactivate slide, set jump impulse.
	// Horizontal momentum is preserved by Barrage (current velocity persists).
	OwnerCMC->DeactivateAbility();
	OwnerCMC->SetPendingJumpImpulse(OwnerCMC->MovementProfile->SlideJumpVelocity);

	return true;
}

bool USlideAbility::HandleCrouchInput(bool bPressed)
{
	bSlideCrouchHeld = bPressed;
	if (!bPressed)
	{
		OwnerCMC->DeactivateAbility();
	}
	return true;
}
