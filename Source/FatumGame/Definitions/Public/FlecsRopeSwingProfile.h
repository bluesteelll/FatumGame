// UFlecsRopeSwingProfile — Data Asset defining rope swing behavior.
// All distances in cm (converted to Jolt meters at prefab creation).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsRopeSwingProfile.generated.h"

UCLASS(BlueprintType, EditInlineNew)
class UFlecsRopeSwingProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// ── Rope geometry ──

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope", meta = (ClampMin = "50"))
	float MaxRopeLength = 400.f; // cm

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope", meta = (ClampMin = "10"))
	float MinGrabLength = 50.f; // cm, closest to anchor

	// ── Swing physics ──

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swing", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float SwingGravityMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swing", meta = (ClampMin = "100", ClampMax = "3000"))
	float SwingInputStrength = 1000.f; // cm/s^2

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swing", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float AirDragCoefficient = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swing", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float ClimbDragMultiplier = 3.f;

	// ── Climb ──

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ClampMin = "50"))
	float ClimbSpeedUp = 150.f; // cm/s

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ClampMin = "50"))
	float ClimbSpeedDown = 200.f; // cm/s

	// ── Jump off ──

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JumpOff", meta = (ClampMin = "0"))
	float JumpOffVerticalBoost = 350.f; // cm/s

	// ── Transitions ──

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transitions", meta = (ClampMin = "0.01"))
	float EnterLerpDuration = 0.12f; // seconds

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transitions", meta = (ClampMin = "0.01"))
	float TopDismountDuration = 0.2f; // seconds

	// ── Input ──

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0.3", ClampMax = "0.95"))
	float SwingClimbThreshold = 0.7f;

	// ── Visual ──

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual", meta = (ClampMin = "3", ClampMax = "16"))
	int32 VerletSegments = 8;
};
