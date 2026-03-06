// Custom CharacterMovementComponent — posture, camera effects, capsule management.
// Ability logic (slide, mantle, blink, jump) runs on sim thread.
// This component reads sim-thread state via atomics and manages game-thread visuals.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "FlecsMovementComponents.h"
#include "FPostureStateMachine.h"
#include "SkeletonTypes.h"
#include "FatumMovementComponent.generated.h"

class UFlecsMovementProfile;
class FBarragePrimitive;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnPostureChanged, ECharacterPosture /*NewPosture*/);

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
	// BARRAGE STATE (fed by ApplyBarrageSync from AFlecsCharacter::Tick)
	// ═══════════════════════════════════════════════════════════════

	void SetBarrageVelocity(const FVector& V) { BarrageVelocity = V; }
	void SetBarrageGroundState(uint8 GS) { BarrageGroundState = GS; }
	const FVector& GetBarrageVelocity() const { return BarrageVelocity; }

	// ═══════════════════════════════════════════════════════════════
	// QUERIES
	// ═══════════════════════════════════════════════════════════════

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	ECharacterPosture GetCurrentPosture() const { return PostureSM.CurrentPosture; }

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	ECharacterMoveMode GetCurrentMoveMode() const { return CurrentMoveMode; }

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	bool IsSprinting() const { return CurrentMoveMode == ECharacterMoveMode::Sprint; }

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	bool IsSliding() const { return bSimSliding; }

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	bool IsMantling() const { return bSimMantling; }

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	bool IsBlinking() const { return false; } // Blink has no game-thread visual state

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	float GetCurrentFOVOffset() const { return CurrentFOVOffset; }

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	float GetLandingCameraOffset() const { return LandingCompressOffset; }

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	float GetCurrentEyeHeight() const { return PostureSM.GetCurrentEyeHeight(); }

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	float GetHeadBobVerticalOffset() const { return HeadBobVerticalOffset; }

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	float GetHeadBobHorizontalOffset() const { return HeadBobHorizontalOffset; }

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	float GetSlideTiltAngle() const { return SlideTiltCurrent; }

	// ═══════════════════════════════════════════════════════════════
	// COMMANDS (called by AFlecsCharacter input handlers)
	// ═══════════════════════════════════════════════════════════════

	void RequestSprint(bool bSprint);
	void RequestCrouch(bool bPressed);
	void RequestProne(bool bPressed);

	// ═══════════════════════════════════════════════════════════════
	// POSTURE / CAMERA / CAPSULE MANAGEMENT
	// ═══════════════════════════════════════════════════════════════

	/** Tick posture, capsule management, and camera effects.
	 *  Called from AFlecsCharacter::Tick() AFTER velocity/GS are fed, BEFORE camera update.
	 *  Ability state bools come from sim-thread atomics (read by AFlecsCharacter). */
	void TickPostureAndEffects(float DeltaTime, bool bSliding, bool bMantling, bool bHanging, uint8 MantleType);

	/** Access PostureSM for external posture queries. */
	FPostureStateMachine& GetPostureSM() { return PostureSM; }
	const FPostureStateMachine& GetPostureSM() const { return PostureSM; }

	/** Public ceiling check (used by posture + capsule restore). */
	bool CanExpandToHeight(float TargetHalfHeight) const;

	/** Broadcast posture changed delegate (for Barrage capsule shape sync). */
	void BroadcastPostureChanged(ECharacterPosture Posture);

	// ═══════════════════════════════════════════════════════════════
	// CHARACTER ACCESSORS (delegate to AFlecsCharacter)
	// ═══════════════════════════════════════════════════════════════

	/** Set feet-to-actor Z offset (mantle forces this to CrouchHalfHeight). */
	void SetFeetToActorOffset(float Value);

	/** Get the character's SkeletonKey (invalid if not an AFlecsCharacter). */
	FSkeletonKey GetCharacterEntityKey() const;

	/** Get the character's cached Barrage body (nullptr if not available). */
	TSharedPtr<FBarragePrimitive> GetCharacterBarrageBody() const;

	/** Get the Flecs Artillery Subsystem (nullptr if not available). */
	class UFlecsArtillerySubsystem* GetFlecsSubsystem() const;

	// ═══════════════════════════════════════════════════════════════
	// DELEGATE
	// ═══════════════════════════════════════════════════════════════

	/** Fires when posture actually changes (after ceiling check). */
	FOnPostureChanged OnPostureChanged;

	// ═══════════════════════════════════════════════════════════════
	// GROUND STATE
	// ═══════════════════════════════════════════════════════════════

	virtual bool IsMovingOnGround() const override { return BarrageGroundState == 0; }
	virtual bool IsFalling() const override { return BarrageGroundState != 0; }

protected:
	// ═══════════════════════════════════════════════════════════════
	// CMC OVERRIDES — passive mode (Barrage is sole physics authority)
	// ═══════════════════════════════════════════════════════════════

	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void PerformMovement(float DeltaTime) override {}

private:
	// ═══════════════════════════════════════════════════════════════
	// BARRAGE STATE (game thread only)
	// ═══════════════════════════════════════════════════════════════

	FVector BarrageVelocity = FVector::ZeroVector;
	uint8 BarrageGroundState = 3; // FBGroundState: 0=OnGround, 1=SteepGround, 2=NotSupported, 3=InAir

	// ═══════════════════════════════════════════════════════════════
	// SIM-THREAD ABILITY STATE (set each tick from atomics)
	// ═══════════════════════════════════════════════════════════════

	bool bSimSliding = false;
	bool bSimMantling = false;
	bool bSimHanging = false;
	bool bPrevSliding = false;
	bool bPrevMantling = false;

	// ═══════════════════════════════════════════════════════════════
	// HSM STATE
	// ═══════════════════════════════════════════════════════════════

	FPostureStateMachine PostureSM;
	ECharacterMoveMode CurrentMoveMode = ECharacterMoveMode::Idle;
	bool bWantsToSprint = false;

	// Landing detection (for camera compress)
	bool bWasGroundedLastFrame = true;
	float LandingFallSpeed = 0.f;

	// Camera effects
	float CurrentFOVOffset = 0.f;
	float TargetFOVOffset = 0.f;
	float LandingCompressTimer = 0.f;
	float LandingCompressOffset = 0.f;
	float LandingCompressInitial = 0.f;

	// Head bob
	float HeadBobTimer = 0.f;
	float HeadBobVerticalOffset = 0.f;
	float HeadBobHorizontalOffset = 0.f;
	float HeadBobAmplitudeScale = 0.f;

	// Slide tilt
	float SlideTiltCurrent = 0.f;

	// ═══════════════════════════════════════════════════════════════
	// HSM LOGIC
	// ═══════════════════════════════════════════════════════════════

	void UpdatePosture(float DeltaTime);
	void UpdateMovementLayer(float DeltaTime);
	void UpdateCameraEffects(float DeltaTime);
	void UpdateHeadBob(float DeltaTime);
	void UpdateSlideTilt(float DeltaTime);
	void TransitionMoveMode(ECharacterMoveMode NewMode);
};
