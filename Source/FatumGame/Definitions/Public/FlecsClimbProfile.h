// Climb profile for Flecs entity spawning.
// Defines parameters for climbable entities (ladders, vines).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsClimbProfile.generated.h"

/**
 * Data Asset defining climb behavior for a climbable entity.
 * All distances in centimeters (converted to Jolt meters on spawn).
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsClimbProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Total climbable height (cm). Should match or be slightly less than the physics body height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ClampMin = "100", ClampMax = "5000"))
	float LadderHeight = 300.f;

	/** Climb speed upward (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ClampMin = "50", ClampMax = "1000"))
	float ClimbSpeed = 200.f;

	/** Climb speed downward (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ClampMin = "50", ClampMax = "1000"))
	float ClimbSpeedDown = 250.f;

	/** Horizontal speed when jumping off ladder (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ClampMin = "50", ClampMax = "1500"))
	float JumpOffHorizontalSpeed = 400.f;

	/** Vertical speed when jumping off ladder (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ClampMin = "50", ClampMax = "1500"))
	float JumpOffVerticalSpeed = 350.f;

	/** Character distance from ladder surface (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ClampMin = "10", ClampMax = "100"))
	float StandoffDistance = 35.f;

	/** Duration of enter lerp when grabbing the ladder (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float EnterLerpDuration = 0.15f;

	/** Duration of top dismount animation (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float TopDismountDuration = 0.2f;

	/** Forward distance from ladder top for dismount endpoint (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ClampMin = "10", ClampMax = "200"))
	float TopDismountForwardDistance = 50.f;
};
