// Interaction state data for AFlecsCharacter.
// Extracted from FlecsCharacter.h for header clarity.

#pragma once

#include "CoreMinimal.h"
#include "SkeletonTypes.h"
#include "FlecsInteractionTypes.h"

class UFlecsInteractionProfile;

/** All interaction state: detection, state machine, focus camera, hold progress. */
struct FCharacterInteractionState
{
	// State machine
	EInteractionState State = EInteractionState::Gameplay;
	const UFlecsInteractionProfile* ActiveProfile = nullptr;
	FSkeletonKey ActiveTargetKey;

	// Detection (10Hz trace results)
	FSkeletonKey CurrentTarget;
	FText CachedPrompt;
	EInteractionType CachedType = EInteractionType::Instant;
	float CachedHoldDuration = 0.f;

	// Focus camera transition
	FTransform SavedCameraTransform = FTransform::Identity;
	float SavedCameraFOV = 90.f;
	FTransform FocusCameraTarget = FTransform::Identity;
	float FocusTargetFOV = 0.f;
	float FocusLerpAlpha = 0.f;
	float CurrentTransitionDuration = 0.4f;

	// Hold state
	float HoldAccumulator = 0.f;
	float HoldRequiredDuration = 1.f;
	float HoldTargetLostTime = 0.f;
	bool bHoldCanCancel = true;
	bool bInteractKeyHeld = false;
};
