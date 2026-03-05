// Shared capsule shrink/restore helpers for movement abilities (Slide, Mantle, etc.).
// Eliminates duplication of capsule resize + actor Z offset + PostureSM sync logic.

#pragma once

#include "CoreMinimal.h"
#include "FlecsMovementComponents.h"

class UFatumMovementComponent;
class UFlecsMovementProfile;
class ACharacter;

namespace AbilityCapsuleHelper
{
	/** Shrink capsule to crouch dimensions. Adjusts actor Z and eye height.
	 *  Sets PostureSM.CurrentPosture = Crouching and broadcasts posture change. */
	void ShrinkToCrouch(ACharacter* Char, UFatumMovementComponent* CMC,
	                    const UFlecsMovementProfile* Profile);

	/** Restore capsule from crouch to best-fit posture (standing if ceiling allows).
	 *  Adjusts actor Z (if grounded), syncs PostureSM, broadcasts posture change.
	 *  @param bAllowStanding  If false, always restore to crouching (e.g. crouch held).
	 *  @return The posture that was chosen. */
	ECharacterPosture RestoreFromCrouch(ACharacter* Char, UFatumMovementComponent* CMC,
	                                    const UFlecsMovementProfile* Profile,
	                                    bool bAllowStanding = true);
}
