// Posture state machine — handles Standing/Crouching/Prone transitions.
// Pure logic, no UE dependencies beyond FMath. Owned by UFatumMovementComponent.

#pragma once

#include "CoreMinimal.h"
#include "FlecsMovementComponents.h"

class UFlecsMovementProfile;

/**
 * Manages posture transitions (Standing/Crouching/Prone) with:
 * - Configurable Toggle/Hold input modes per posture
 * - Ceiling clearance checks before expanding upward
 * - Smooth eye height interpolation
 */
struct FPostureStateMachine
{
	// ═══════════════════════════════════════════════════════════════
	// STATE
	// ═══════════════════════════════════════════════════════════════

	ECharacterPosture CurrentPosture = ECharacterPosture::Standing;
	float CurrentEyeHeight = 60.f;
	float TargetEyeHeight = 60.f;

	// ═══════════════════════════════════════════════════════════════
	// API
	// ═══════════════════════════════════════════════════════════════

	/** Call on crouch input. For Hold mode: true on press, false on release.
	 *  For Toggle mode: true on press (toggles internally), release ignored. */
	void RequestCrouch(bool bPressed, const UFlecsMovementProfile* Profile);

	/** Call on prone input. For Toggle mode: toggles on press.
	 *  For Hold mode: true on press, false on release. */
	void RequestProne(bool bPressed, const UFlecsMovementProfile* Profile);

	/** Force-clear crouch state (including toggle). Used by crouch-jump. */
	void ForceClearCrouch();

	/** Tick the state machine. Returns true if posture actually changed this tick. */
	bool Tick(float DeltaTime, const UFlecsMovementProfile* Profile,
		bool bCanStandUp, bool bCanCrouch);

	// ═══════════════════════════════════════════════════════════════
	// QUERIES
	// ═══════════════════════════════════════════════════════════════

	float GetCurrentEyeHeight() const { return CurrentEyeHeight; }
	bool IsFullyTransitioned() const { return FMath::IsNearlyEqual(CurrentEyeHeight, TargetEyeHeight, 0.1f); }

private:
	bool bWantsCrouch = false;
	bool bWantsProne = false;
	bool bCrouchToggled = false;  // internal toggle state for crouch
	bool bProneToggled = false;   // internal toggle state for prone

	ECharacterPosture DetermineDesiredPosture() const;
};
