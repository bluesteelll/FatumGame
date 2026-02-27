// Custom CharacterMovementComponent with hierarchical state machine.
// Handles sprint, posture, jump buffering, coyote time.
// Movement abilities (Slide, Dash, etc.) are modular UMovementAbility subclasses.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "FlecsMovementComponents.h"
#include "FPostureStateMachine.h"
#include "FatumMovementComponent.generated.h"

class UFlecsMovementProfile;
class UMovementAbility;

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

	/** Set pending jump impulse in cm/s (consumed by FlecsCharacter → OtherForce). */
	void SetPendingJumpImpulse(float V) { PendingJumpImpulse = V; }
	float ConsumePendingJumpImpulse() { float V = PendingJumpImpulse; PendingJumpImpulse = 0.f; return V; }

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
	bool IsSliding() const;

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	float GetCurrentFOVOffset() const { return CurrentFOVOffset; }

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	float GetLandingCameraOffset() const { return LandingCompressOffset; }

	UFUNCTION(BlueprintPure, Category = "Fatum|Movement")
	float GetCurrentEyeHeight() const { return PostureSM.GetCurrentEyeHeight(); }

	// ═══════════════════════════════════════════════════════════════
	// COMMANDS (called by AFlecsCharacter input handlers or AI)
	// ═══════════════════════════════════════════════════════════════

	void RequestSprint(bool bSprint);
	void RequestJump();
	void ReleaseJump();
	void RequestCrouch(bool bPressed);
	void RequestProne(bool bPressed);

	// ═══════════════════════════════════════════════════════════════
	// ABILITY SYSTEM
	// ═══════════════════════════════════════════════════════════════

	/** Tick posture, abilities, and camera effects. Called from AFlecsCharacter::Tick()
	 *  AFTER velocity/GS are fed, BEFORE camera update. Ensures fresh eye height for camera. */
	void TickPostureAndEffects(float DeltaTime);

	/** Access PostureSM for abilities that own posture. */
	FPostureStateMachine& GetPostureSM() { return PostureSM; }
	const FPostureStateMachine& GetPostureSM() const { return PostureSM; }

	/** Public ceiling check for abilities. */
	bool CanExpandToHeight(float TargetHalfHeight) const;

	/** Broadcast posture changed delegate (for abilities that change capsule). */
	void BroadcastPostureChanged(ECharacterPosture Posture);

	/** Find a registered ability by type. Returns nullptr if not found. */
	template<typename T>
	T* FindAbility() const
	{
		for (UMovementAbility* Ability : Abilities)
		{
			if (T* Typed = Cast<T>(Ability))
			{
				return Typed;
			}
		}
		return nullptr;
	}

	/** Activate an ability (deactivates current if any). */
	void ActivateAbility(UMovementAbility* Ability);

	/** Deactivate the current active ability. */
	void DeactivateAbility();

	/** Get the currently active ability (nullptr if none). */
	UMovementAbility* GetActiveAbility() const { return ActiveAbility; }

	// ═══════════════════════════════════════════════════════════════
	// DELEGATE
	// ═══════════════════════════════════════════════════════════════

	/** Fires when posture actually changes (after ceiling check). */
	FOnPostureChanged OnPostureChanged;

	// ═══════════════════════════════════════════════════════════════
	// GROUND STATE (public — used by FlecsCharacter, SlideAbility, etc.)
	// ═══════════════════════════════════════════════════════════════

	virtual bool IsMovingOnGround() const override { return BarrageGroundState == 0; }
	virtual bool IsFalling() const override { return BarrageGroundState != 0; }

protected:
	// ═══════════════════════════════════════════════════════════════
	// CMC OVERRIDES — passive mode (Barrage is sole physics authority)
	// ═══════════════════════════════════════════════════════════════

	virtual void InitializeComponent() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void PerformMovement(float DeltaTime) override {}

private:
	// ═══════════════════════════════════════════════════════════════
	// ABILITY REGISTRY
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY()
	TArray<TObjectPtr<UMovementAbility>> Abilities;

	UPROPERTY()
	TObjectPtr<UMovementAbility> ActiveAbility;

	void RegisterAbility(TSubclassOf<UMovementAbility> AbilityClass);

	// ═══════════════════════════════════════════════════════════════
	// BARRAGE STATE (game thread only)
	// ═══════════════════════════════════════════════════════════════

	FVector BarrageVelocity = FVector::ZeroVector;
	uint8 BarrageGroundState = 3; // FBGroundState: 0=OnGround, 1=SteepGround, 2=NotSupported, 3=InAir
	float PendingJumpImpulse = 0.f;

	// ═══════════════════════════════════════════════════════════════
	// HSM STATE
	// ═══════════════════════════════════════════════════════════════

	FPostureStateMachine PostureSM;
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
	void UpdatePosture(float DeltaTime);
	void UpdateMovementLayer(float DeltaTime);
	void UpdateCameraEffects(float DeltaTime);
	void TransitionMoveMode(ECharacterMoveMode NewMode);

	static constexpr float SimFrameTime = 1.f / 60.f;
};
