// Slide ability — thin game-thread wrapper.
// Physics (deceleration, timer, exit conditions) runs on sim thread via FSlideInstance
// in PrepareCharacterStep. This class handles visuals, capsule, and input routing.

#include "SlideAbility.h"
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

	// Visual: shrink capsule to crouch dimensions
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

	// Set posture to Crouching so external queries see correct state
	OwnerCMC->GetPostureSM().CurrentPosture = ECharacterPosture::Crouching;
	OwnerCMC->GetPostureSM().TargetEyeHeight = Profile->SlideEyeHeight;

	// Sync Barrage capsule shape
	OwnerCMC->BroadcastPostureChanged(ECharacterPosture::Crouching);
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

	// Visual: determine target posture after slide
	ECharacterPosture TargetPosture;
	if (bSlideCrouchHeld)
	{
		TargetPosture = ECharacterPosture::Crouching;
	}
	else if (OwnerCMC->CanExpandToHeight(Profile->StandingHalfHeight))
	{
		TargetPosture = ECharacterPosture::Standing;
	}
	else
	{
		TargetPosture = ECharacterPosture::Crouching;
	}

	float OldHH = Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	float R, HH;
	Profile->GetCapsuleForPosture(TargetPosture, R, HH);
	Char->GetCapsuleComponent()->SetCapsuleSize(R, HH, true);

	float HeightDelta = HH - OldHH;
	if (!FMath::IsNearlyZero(HeightDelta) && OwnerCMC->IsMovingOnGround())
	{
		FVector Loc = Char->GetActorLocation();
		Loc.Z += HeightDelta;
		Char->SetActorLocation(Loc);

		OwnerCMC->GetPostureSM().CurrentEyeHeight -= HeightDelta;
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

	OwnerCMC->BroadcastPostureChanged(TargetPosture);
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

void USlideAbility::OnCrouchInput(bool bPressed)
{
	bSlideCrouchHeld = bPressed;
	if (!bPressed)
	{
		OwnerCMC->DeactivateAbility();
	}
}
