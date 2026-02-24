// Custom CharacterMovementComponent with hierarchical state machine.
// Handles sprint, crouch, jump buffering, coyote time.
// Phase 1: Walk/Run/Sprint/Jump with coyote+buffer.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "FlecsMovementComponents.h"
#include "FatumMovementComponent.generated.h"

class UFlecsMovementProfile;

UCLASS()
class FATUMGAME_API UFatumMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UFatumMovementComponent();

	// ═══════════════════════════════════════════════════════════════
	// PROFILE
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fatum|Movement")
	TObjectPtr<UFlecsMovementProfile> MovementProfile;

	/** Apply profile values to CMC. Call after setting MovementProfile. */
	void ApplyProfile();

	// ═══════════════════════════════════════════════════════════════
	// QUERIES
	// ═══════════════════════════════════════════════════════════════

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	ECharacterPosture GetCurrentPosture() const { return CurrentPosture; }

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	ECharacterMoveMode GetCurrentMoveMode() const { return CurrentMoveMode; }

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	bool IsSprinting() const { return CurrentMoveMode == ECharacterMoveMode::Sprint; }

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	float GetCurrentFOVOffset() const { return CurrentFOVOffset; }

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	float GetLandingCameraOffset() const { return LandingCompressOffset; }

	// ═══════════════════════════════════════════════════════════════
	// COMMANDS (called by AFlecsCharacter input handlers)
	// ═══════════════════════════════════════════════════════════════

	void RequestSprint(bool bSprint);
	void RequestJump();
	void ReleaseJump();

protected:
	// ═══════════════════════════════════════════════════════════════
	// CMC OVERRIDES
	// ═══════════════════════════════════════════════════════════════

	virtual void InitializeComponent() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual float GetMaxSpeed() const override;
	virtual float GetMaxAcceleration() const override;
	virtual float GetMaxBrakingDeceleration() const override;

private:
	// ═══════════════════════════════════════════════════════════════
	// HSM STATE
	// ═══════════════════════════════════════════════════════════════

	ECharacterPosture CurrentPosture = ECharacterPosture::Standing;
	ECharacterMoveMode CurrentMoveMode = ECharacterMoveMode::Idle;
	bool bWantsToSprint = false;

	// Jump buffering
	float CoyoteTimer = 0.f;
	float JumpBufferTimer = 0.f;
	bool bWasGroundedLastFrame = true;
	bool bJumpedIntentionally = false;

	// Camera effects
	float CurrentFOVOffset = 0.f;
	float TargetFOVOffset = 0.f;
	float LandingCompressTimer = 0.f;
	float LandingCompressOffset = 0.f;
	float LandingCompressInitial = 0.f;
	float LandingFallSpeed = 0.f;

	// ═══════════════════════════════════════════════════════════════
	// HSM LOGIC
	// ═══════════════════════════════════════════════════════════════

	void TickHSM(float DeltaTime);
	void UpdateMovementLayer(float DeltaTime);
	void UpdateCameraEffects(float DeltaTime);
	void TransitionMoveMode(ECharacterMoveMode NewMode);

	static constexpr float SimFrameTime = 1.f / 60.f;
};
