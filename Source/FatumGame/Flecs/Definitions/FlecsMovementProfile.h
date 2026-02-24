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

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

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
