// Shared capsule shrink/restore helpers for movement abilities.

#include "AbilityCapsuleHelper.h"
#include "FatumMovementComponent.h"
#include "FlecsMovementProfile.h"
#include "FPostureStateMachine.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

void AbilityCapsuleHelper::ShrinkToCrouch(ACharacter* Char, UFatumMovementComponent* CMC,
                                          const UFlecsMovementProfile* Profile)
{
	check(Char && CMC && Profile);

	float OldHH = Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Char->GetCapsuleComponent()->SetCapsuleSize(
		Profile->CrouchRadius, Profile->CrouchHalfHeight, true);

	float HeightDelta = Profile->CrouchHalfHeight - OldHH;
	if (!FMath::IsNearlyZero(HeightDelta))
	{
		FVector Loc = Char->GetActorLocation();
		Loc.Z += HeightDelta;
		Char->SetActorLocation(Loc);
		CMC->GetPostureSM().CurrentEyeHeight -= HeightDelta;
	}

	// Set posture to Crouching so external queries see correct state
	CMC->GetPostureSM().CurrentPosture = ECharacterPosture::Crouching;

	// Sync Barrage capsule shape
	CMC->BroadcastPostureChanged(ECharacterPosture::Crouching);
}

ECharacterPosture AbilityCapsuleHelper::RestoreFromCrouch(ACharacter* Char, UFatumMovementComponent* CMC,
                                                          const UFlecsMovementProfile* Profile,
                                                          bool bAllowStanding)
{
	check(Char && CMC && Profile);

	// Determine target posture
	ECharacterPosture TargetPosture;
	if (bAllowStanding && CMC->CanExpandToHeight(Profile->StandingHalfHeight))
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

	// Adjust actor Z for height delta (only if grounded)
	float HeightDelta = HH - OldHH;
	if (!FMath::IsNearlyZero(HeightDelta) && CMC->IsMovingOnGround())
	{
		FVector Loc = Char->GetActorLocation();
		Loc.Z += HeightDelta;
		Char->SetActorLocation(Loc);
		CMC->GetPostureSM().CurrentEyeHeight -= HeightDelta;
	}

	// Sync PostureSM state
	FPostureStateMachine& SM = CMC->GetPostureSM();
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
	CMC->BroadcastPostureChanged(TargetPosture);

	return TargetPosture;
}
