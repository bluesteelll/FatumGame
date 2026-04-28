// Interaction state data for AFlecsCharacter.
// Extracted from FlecsCharacter.h for header clarity.

#pragma once

#include "CoreMinimal.h"
#include "SkeletonTypes.h"
#include "FlecsInteractionTypes.h"
#include "FlecsCraftingTypes.h"  // Phase 4 — EWrenchHoverKind

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

	// Crafting station hover (parallel to CurrentTarget — UI-only, no interaction dispatch).
	// Written in PerformInteractionTrace when a FTagCraftingStation entity is hit;
	// cleared to Invalid() otherwise. Read via AFlecsCharacter::GetCraftingHoverTarget.
	FSkeletonKey CraftingHoverTarget;

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

	// ─── Phase 4 — wrench-aware hover classification ─────────────
	// Written by PerformInteractionTrace each tick; read by HandleInteractionInput
	// to route wrench-active E presses to attach/detach/deconstruct dispatchers.
	EWrenchHoverKind WrenchHoverKind = EWrenchHoverKind::NotApplicable;
	int64            WrenchHoverTargetId = 0;   // Flecs entity id of hovered child OR station
	int32            WrenchHoverPortIndex = -1; // index into Blueprint->ExtensionPorts (Empty/Occupied port)

	// ─── Phase 4 — Hold-E wrench-deconstruct sub-state ───────────
	// True when a Hold-E session was started by wrench-on-anchor; on completion
	// dispatches RequestStationDeconstruct instead of the legacy interaction path.
	bool bHoldIsWrenchDeconstruct = false;
};
