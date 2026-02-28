// Movement tuning parameters Data Asset.
// Attached to FlecsEntityDefinition for characters, or set directly on AFlecsCharacter.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsMovementComponents.h"
#include "FlecsMovementProfile.generated.h"

UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsMovementProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// SPEED
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0", ClampMax = "2000"))
	float WalkSpeed = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0", ClampMax = "2000"))
	float SprintSpeed = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0", ClampMax = "1000"))
	float CrouchSpeed = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0", ClampMax = "500"))
	float ProneSpeed = 60.f;

	// ═══════════════════════════════════════════════════════════════
	// ACCELERATION
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acceleration", meta = (ClampMin = "0", ClampMax = "10000"))
	float GroundAcceleration = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acceleration", meta = (ClampMin = "0", ClampMax = "10000"))
	float GroundDeceleration = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acceleration", meta = (ClampMin = "0", ClampMax = "5000"))
	float AirAcceleration = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acceleration", meta = (ClampMin = "0", ClampMax = "10000"))
	float SprintAcceleration = 2500.f;

	// ═══════════════════════════════════════════════════════════════
	// JUMP
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump", meta = (ClampMin = "0", ClampMax = "2000"))
	float JumpVelocity = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump", meta = (ClampMin = "0", ClampMax = "2000"))
	float CrouchJumpVelocity = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump", meta = (ClampMin = "0", ClampMax = "30"))
	int32 CoyoteTimeFrames = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump", meta = (ClampMin = "0", ClampMax = "30"))
	int32 JumpBufferFrames = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump", meta = (ClampMin = "0", ClampMax = "1"))
	float AirControlMultiplier = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jump", meta = (ClampMin = "0", ClampMax = "5"))
	float GravityScale = 1.f;

	// ═══════════════════════════════════════════════════════════════
	// CAPSULE -- UE units (cm)
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule", meta = (ClampMin = "10", ClampMax = "100"))
	float StandingRadius = 34.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule", meta = (ClampMin = "20", ClampMax = "200"))
	float StandingHalfHeight = 88.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule", meta = (ClampMin = "10", ClampMax = "100"))
	float CrouchRadius = 34.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule", meta = (ClampMin = "20", ClampMax = "150"))
	float CrouchHalfHeight = 55.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule", meta = (ClampMin = "10", ClampMax = "100"))
	float ProneRadius = 34.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule", meta = (ClampMin = "10", ClampMax = "100"))
	float ProneHalfHeight = 30.f;

	// ═══════════════════════════════════════════════════════════════
	// CAMERA -- First-person effects
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0", ClampMax = "30"))
	float SprintFOVBoost = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "1", ClampMax = "20"))
	float FOVInterpSpeed = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0", ClampMax = "1"))
	float LandingCameraCompressDuration = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0", ClampMax = "20"))
	float LandingCameraCompressAmount = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0", ClampMax = "2000"))
	float LandingMinFallSpeed = 300.f;

	// ── Head Bob ──

	/** Vertical bob amplitude (cm). Vert freq = 2x horiz freq → figure-8 Lissajous. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|HeadBob", meta = (ClampMin = "0", ClampMax = "15"))
	float BobVerticalAmplitude = 3.f;

	/** Horizontal bob amplitude (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|HeadBob", meta = (ClampMin = "0", ClampMax = "10"))
	float BobHorizontalAmplitude = 1.5f;

	/** Vertical oscillation frequency (Hz). Horizontal = half this.
	 *  ~2 Hz = walking pace (~4 steps/sec, half-cycle per step). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|HeadBob", meta = (ClampMin = "0.5", ClampMax = "10"))
	float BobVerticalFrequency = 2.f;

	/** Amplitude multiplier during sprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|HeadBob", meta = (ClampMin = "0.5", ClampMax = "3"))
	float BobSprintMultiplier = 1.4f;

	/** Amplitude multiplier during crouch walk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|HeadBob", meta = (ClampMin = "0", ClampMax = "1"))
	float BobCrouchMultiplier = 0.5f;

	/** FInterpTo speed for bob amplitude fade in/out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|HeadBob", meta = (ClampMin = "1", ClampMax = "30"))
	float BobInterpSpeed = 10.f;

	// ── Slide Tilt ──

	/** Max camera roll during slide (degrees). Direction follows lateral velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|SlideTilt", meta = (ClampMin = "0", ClampMax = "15"))
	float SlideCameraTiltAngle = 5.f;

	/** FInterpTo speed for slide tilt transition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|SlideTilt", meta = (ClampMin = "1", ClampMax = "30"))
	float SlideTiltInterpSpeed = 8.f;

	// ═══════════════════════════════════════════════════════════════
	// POSTURE
	// ═══════════════════════════════════════════════════════════════

	/** false = Hold (default), true = Toggle */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Posture")
	bool bCrouchIsToggle = false;

	/** true = Toggle (default), false = Hold */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Posture")
	bool bProneIsToggle = true;

	/** Camera transition speed: Crouch/Prone → Standing (FInterpTo speed) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Posture", meta = (ClampMin = "1", ClampMax = "30"))
	float StandUpSpeed = 12.f;

	/** Camera transition speed: Standing/Prone → Crouching */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Posture", meta = (ClampMin = "1", ClampMax = "30"))
	float CrouchTransitionSpeed = 14.f;

	/** Camera transition speed: Any → Prone */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Posture", meta = (ClampMin = "1", ClampMax = "30"))
	float ProneTransitionSpeed = 8.f;

	/** Eye height when standing (cm, relative to actor root) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Posture", meta = (ClampMin = "5", ClampMax = "100"))
	float StandingEyeHeight = 60.f;

	/** Eye height when crouching (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Posture", meta = (ClampMin = "5", ClampMax = "80"))
	float CrouchEyeHeight = 35.f;

	/** Eye height when prone (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Posture", meta = (ClampMin = "5", ClampMax = "50"))
	float ProneEyeHeight = 15.f;

	// ═══════════════════════════════════════════════════════════════
	// SLIDE
	// ═══════════════════════════════════════════════════════════════

	/** Minimum horizontal speed to initiate a slide (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = "100", ClampMax = "2000"))
	float SlideMinEntrySpeed = 500.f;

	/** Speed boost added on slide initiation (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = "0", ClampMax = "500"))
	float SlideInitialSpeedBoost = 50.f;

	/** Deceleration on flat ground during slide (cm/s^2) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = "50", ClampMax = "2000"))
	float SlideDeceleration = 400.f;

	/** Speed below which slide auto-ends (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = "0", ClampMax = "500"))
	float SlideMinExitSpeed = 100.f;

	/** Maximum slide duration — safety cap (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float SlideMaxDuration = 1.5f;

	/** Jump Z velocity when cancelling slide with jump (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = "0", ClampMax = "2000"))
	float SlideJumpVelocity = 500.f;

	/** Camera eye height during slide (cm, relative to actor root) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = "5", ClampMax = "50"))
	float SlideEyeHeight = 25.f;

	/** Eye height transition speed into/out of slide (FInterpTo) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = "1", ClampMax = "30"))
	float SlideTransitionSpeed = 16.f;

	/** Steering acceleration during slide (cm/s^2) — controls how fast input redirects slide direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = "0", ClampMax = "1000"))
	float SlideMinAcceleration = 100.f;

	/** Ground friction override during slide (low = slippery). Multiplied by 100 for BrakingDeceleration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (ClampMin = "0", ClampMax = "1"))
	float SlideGroundFriction = 0.1f;

	// ═══════════════════════════════════════════════════════════════
	// MANTLE / VAULT
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mantle", meta = (ClampMin = "40", ClampMax = "150"))
	float MantleForwardReach = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mantle", meta = (ClampMin = "20", ClampMax = "100"))
	float MantleMinHeight = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mantle", meta = (ClampMin = "50", ClampMax = "200"))
	float MantleVaultMaxHeight = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mantle", meta = (ClampMin = "100", ClampMax = "350"))
	float MantleMaxHeight = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mantle", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float MantleRiseDuration = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mantle", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float MantlePullDuration = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mantle", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float MantleLandDuration = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mantle", meta = (ClampMin = "0", ClampMax = "1"))
	float VaultSpeedMultiplier = 0.8f;

	/** Eye height during mantle/vault transition */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mantle", meta = (ClampMin = "5", ClampMax = "80"))
	float MantleEyeHeight = 35.f;

	/** FInterpTo speed for mantle eye height */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mantle", meta = (ClampMin = "1", ClampMax = "30"))
	float MantleTransitionSpeed = 14.f;

	// ═══════════════════════════════════════════════════════════════
	// LEDGE GRAB
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LedgeGrab", meta = (ClampMin = "100", ClampMax = "300"))
	float LedgeGrabMaxHeight = 236.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LedgeGrab", meta = (ClampMin = "5", ClampMax = "30"))
	float LedgeGrabDetectionRadius = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LedgeGrab", meta = (ClampMin = "5", ClampMax = "50"))
	float LedgeGrabMinLedgeDepth = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LedgeGrab", meta = (ClampMin = "0.05", ClampMax = "0.3"))
	float LedgeGrabTransitionDuration = 0.13f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LedgeGrab")
	bool bUseLedgeHangTimeout = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LedgeGrab", meta = (ClampMin = "1", ClampMax = "30", EditCondition = "bUseLedgeHangTimeout"))
	float LedgeHangMaxDuration = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LedgeGrab", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float LedgeGrabCooldown = 0.3f;

	/** Eye height while hanging (cm below ledge top) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LedgeGrab", meta = (ClampMin = "10", ClampMax = "40"))
	float LedgeHangCameraBelowLedge = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LedgeGrab", meta = (ClampMin = "100", ClampMax = "800"))
	float WallJumpHorizontalForce = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LedgeGrab", meta = (ClampMin = "100", ClampMax = "800"))
	float WallJumpVerticalForce = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LedgeGrab", meta = (ClampMin = "0.2", ClampMax = "1.0"))
	float PullUpDuration = 0.45f;

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	float GetEyeHeightForPosture(ECharacterPosture Posture) const
	{
		switch (Posture)
		{
		case ECharacterPosture::Crouching: return CrouchEyeHeight;
		case ECharacterPosture::Prone:     return ProneEyeHeight;
		default:                           return StandingEyeHeight;
		}
	}

	float GetTransitionSpeed(ECharacterPosture TargetPosture) const
	{
		switch (TargetPosture)
		{
		case ECharacterPosture::Crouching: return CrouchTransitionSpeed;
		case ECharacterPosture::Prone:     return ProneTransitionSpeed;
		default:                           return StandUpSpeed;
		}
	}

	void GetCapsuleForPosture(ECharacterPosture Posture, float& OutRadius, float& OutHalfHeight) const
	{
		switch (Posture)
		{
		case ECharacterPosture::Crouching:
			OutRadius = CrouchRadius;
			OutHalfHeight = CrouchHalfHeight;
			break;
		case ECharacterPosture::Prone:
			OutRadius = ProneRadius;
			OutHalfHeight = ProneHalfHeight;
			break;
		default:
			OutRadius = StandingRadius;
			OutHalfHeight = StandingHalfHeight;
			break;
		}
	}
};
