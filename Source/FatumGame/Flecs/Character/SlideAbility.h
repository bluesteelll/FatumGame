// Slide ability — sprint-to-slide mechanic.
// Activated when sprinting + crouch at sufficient speed.
// Physics (deceleration, timer, exit) runs on sim thread via FSlideInstance.
// This class is a thin game-thread wrapper for visuals, capsule, and input routing.

#pragma once

#include "CoreMinimal.h"
#include "MovementAbility.h"
#include "SlideAbility.generated.h"

UCLASS()
class FATUMGAME_API USlideAbility : public UMovementAbility
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// LIFECYCLE
	// ═══════════════════════════════════════════════════════════════

	virtual bool CanActivate() const override;
	virtual void OnActivated() override;
	virtual void OnTick(float DeltaTime) override;
	virtual void OnDeactivated() override;

	// ═══════════════════════════════════════════════════════════════
	// OVERRIDES
	// ═══════════════════════════════════════════════════════════════

	virtual float GetTargetFOVOffset() const override;
	virtual bool OwnsPosture() const override { return true; }
	virtual ECharacterMoveMode GetMoveMode() const override { return ECharacterMoveMode::Slide; }

	// ═══════════════════════════════════════════════════════════════
	// INPUT INTERCEPTION
	// ═══════════════════════════════════════════════════════════════

	virtual bool HandleJumpRequest() override;

	/** Called by CMC on crouch input while slide is active. */
	void OnCrouchInput(bool bPressed);

private:
	bool bSlideCrouchHeld = false;
};
