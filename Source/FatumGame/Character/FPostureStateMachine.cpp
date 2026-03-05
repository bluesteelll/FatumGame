// Posture state machine implementation.

#include "FPostureStateMachine.h"
#include "FlecsMovementProfile.h"

void FPostureStateMachine::RequestCrouch(bool bPressed, const UFlecsMovementProfile* Profile)
{
	check(Profile);

	if (Profile->bCrouchIsToggle)
	{
		// Toggle: flip on press, ignore release
		if (bPressed)
		{
			bCrouchToggled = !bCrouchToggled;
		}
		bWantsCrouch = bCrouchToggled;
	}
	else
	{
		// Hold: direct mapping
		bWantsCrouch = bPressed;
	}
}

void FPostureStateMachine::ForceClearCrouch()
{
	bWantsCrouch = false;
	bCrouchToggled = false;
}

void FPostureStateMachine::RequestProne(bool bPressed, const UFlecsMovementProfile* Profile)
{
	check(Profile);

	if (Profile->bProneIsToggle)
	{
		// Toggle: flip on press, ignore release
		if (bPressed)
		{
			bProneToggled = !bProneToggled;
		}
		bWantsProne = bProneToggled;
	}
	else
	{
		// Hold: direct mapping
		bWantsProne = bPressed;
	}
}

ECharacterPosture FPostureStateMachine::DetermineDesiredPosture() const
{
	// Prone has priority over crouch
	if (bWantsProne)  return ECharacterPosture::Prone;
	if (bWantsCrouch) return ECharacterPosture::Crouching;
	return ECharacterPosture::Standing;
}

bool FPostureStateMachine::Tick(float DeltaTime, const UFlecsMovementProfile* Profile,
	bool bCanStandUp, bool bCanCrouch)
{
	check(Profile);

	ECharacterPosture DesiredPosture = DetermineDesiredPosture();
	bool bPostureChanged = false;

	if (DesiredPosture != CurrentPosture)
	{
		// Expanding upward requires ceiling clearance
		// Posture order: Standing(0) < Crouching(1) < Prone(2)
		// Expanding = going to a lower enum value (taller posture)
		bool bExpanding = static_cast<uint8>(DesiredPosture) < static_cast<uint8>(CurrentPosture);

		if (bExpanding)
		{
			if (DesiredPosture == ECharacterPosture::Standing && !bCanStandUp)
			{
				// Can't stand fully — try crouching if we're prone
				if (CurrentPosture == ECharacterPosture::Prone && bCanCrouch)
				{
					DesiredPosture = ECharacterPosture::Crouching;
				}
				else
				{
					DesiredPosture = CurrentPosture; // stay put
				}
			}
			else if (DesiredPosture == ECharacterPosture::Crouching && !bCanCrouch)
			{
				DesiredPosture = CurrentPosture; // stay put
			}
		}

		if (DesiredPosture != CurrentPosture)
		{
			CurrentPosture = DesiredPosture;
			TargetEyeHeight = Profile->GetEyeHeightForPosture(DesiredPosture);
			bPostureChanged = true;
		}
	}

	// Interpolate eye height toward target
	float Speed = Profile->GetTransitionSpeed(CurrentPosture);
	CurrentEyeHeight = FMath::FInterpTo(CurrentEyeHeight, TargetEyeHeight, DeltaTime, Speed);

	return bPostureChanged;
}
