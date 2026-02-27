// Base class for modular movement abilities (Slide, Dash, etc.).
// Each ability encapsulates its own state, lifecycle, and speed/camera overrides.
// Owned by UFatumMovementComponent — only one exclusive ability active at a time.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FlecsMovementComponents.h"
#include "MovementAbility.generated.h"

class UFatumMovementComponent;

UCLASS(Abstract)
class FATUMGAME_API UMovementAbility : public UObject
{
	GENERATED_BODY()

public:
	/** Called once when registered on the CMC. */
	void Initialize(UFatumMovementComponent* InOwner);

	// ═══════════════════════════════════════════════════════════════
	// LIFECYCLE (called by CMC)
	// ═══════════════════════════════════════════════════════════════

	/** Can this ability activate right now? */
	virtual bool CanActivate() const { return false; }

	/** Called when ability becomes active. Set up state, capsule, etc. */
	virtual void OnActivated() {}

	/** Called every tick while active. */
	virtual void OnTick(float DeltaTime) {}

	/** Called when ability is deactivated. Restore state, capsule, etc. */
	virtual void OnDeactivated() {}

	// ═══════════════════════════════════════════════════════════════
	// SPEED OVERRIDES (return < 0 to not override)
	// ═══════════════════════════════════════════════════════════════

	virtual float GetMaxSpeedOverride() const { return -1.f; }
	virtual float GetMaxAccelerationOverride() const { return -1.f; }
	virtual float GetMaxBrakingOverride() const { return -1.f; }

	// ═══════════════════════════════════════════════════════════════
	// CAMERA
	// ═══════════════════════════════════════════════════════════════

	/** FOV offset this ability wants (0 = no change). */
	virtual float GetTargetFOVOffset() const { return 0.f; }

	// ═══════════════════════════════════════════════════════════════
	// POSTURE OWNERSHIP
	// ═══════════════════════════════════════════════════════════════

	/** If true, CMC skips PostureSM.Tick while this ability is active. */
	virtual bool OwnsPosture() const { return false; }

	// ═══════════════════════════════════════════════════════════════
	// INPUT INTERCEPTION
	// ═══════════════════════════════════════════════════════════════

	/** Return true if this ability consumed the jump request. */
	virtual bool HandleJumpRequest() { return false; }

	// ═══════════════════════════════════════════════════════════════
	// IDENTITY
	// ═══════════════════════════════════════════════════════════════

	/** The ECharacterMoveMode this ability represents (for ECS sync). */
	virtual ECharacterMoveMode GetMoveMode() const { return ECharacterMoveMode::Idle; }

	bool IsActive() const { return bActive; }

protected:
	UPROPERTY()
	TObjectPtr<UFatumMovementComponent> OwnerCMC;

	bool bActive = false;

	// CMC manages activation state directly
	friend class UFatumMovementComponent;
};
