// Vitals profile for Flecs entity spawning.
// Defines hunger, thirst, and warmth drain rates + severity thresholds.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsVitalsProfile.generated.h"

/**
 * Editor-facing threshold configuration for a single vital severity level.
 * Copied into FVitalThreshold (pure C++) at prefab creation time.
 */
USTRUCT(BlueprintType)
struct FVitalThresholdConfig
{
	GENERATED_BODY()

	/** Vital percent boundary (0.0–1.0). When vital drops below this, modifiers activate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threshold", meta = (ClampMin = "0", ClampMax = "1"))
	float ThresholdPercent = 1.f;

	/** Movement speed multiplier at this severity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threshold")
	float SpeedMultiplier = 1.f;

	/** HP drain per second at this severity (0 = no drain) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threshold", meta = (ClampMin = "0"))
	float HPDrainPerSecond = 0.f;

	/** Can the player sprint at this severity? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threshold")
	bool bCanSprint = true;

	/** Can the player jump at this severity? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threshold")
	bool bCanJump = true;

	/** Multiplier for cross-vital drain at this severity (e.g., cold increases hunger drain) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Threshold", meta = (ClampMin = "0",
		ToolTip = "Multiplier for cross-vital drain at this severity, e.g. Cold increases Hunger drain"))
	float CrossVitalDrainMultiplier = 1.f;
};

/**
 * Data Asset defining vitals properties for entity spawning.
 *
 * Entities with VitalsProfile have hunger, thirst, and warmth needs.
 * Drain rates define how fast each vital decreases over time.
 * Thresholds define severity levels with movement/HP penalties.
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsVitalsProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// HUNGER
	// ═══════════════════════════════════════════════════════════════

	/** Hunger drain per second (default ~5 min to empty) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunger", meta = (ClampMin = "0"))
	float HungerDrainPerSecond = 0.00333f;

	/** Severity thresholds for hunger (up to 4: OK, Low, Critical, Lethal) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hunger")
	TArray<FVitalThresholdConfig> HungerThresholds;

	// ═══════════════════════════════════════════════════════════════
	// THIRST
	// ═══════════════════════════════════════════════════════════════

	/** Thirst drain per second (default ~3.3 min to empty) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thirst", meta = (ClampMin = "0"))
	float ThirstDrainPerSecond = 0.005f;

	/** Severity thresholds for thirst (up to 4: OK, Low, Critical, Lethal) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thirst")
	TArray<FVitalThresholdConfig> ThirstThresholds;

	// ═══════════════════════════════════════════════════════════════
	// WARMTH
	// ═══════════════════════════════════════════════════════════════

	/** Severity thresholds for warmth (up to 4: OK, Low, Critical, Lethal) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warmth")
	TArray<FVitalThresholdConfig> WarmthThresholds;

	/** How fast warmth lerps toward target temperature (percent/sec) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warmth", meta = (ClampMin = "0"))
	float WarmthTransitionSpeed = 0.05f;

	// ═══════════════════════════════════════════════════════════════
	// STARTING VALUES
	// ═══════════════════════════════════════════════════════════════

	/** Starting hunger (1.0 = full) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Starting Values", meta = (ClampMin = "0", ClampMax = "1"))
	float StartingHunger = 1.f;

	/** Starting thirst (1.0 = full) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Starting Values", meta = (ClampMin = "0", ClampMax = "1"))
	float StartingThirst = 1.f;

	/** Starting warmth (1.0 = warm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Starting Values", meta = (ClampMin = "0", ClampMax = "1"))
	float StartingWarmth = 1.f;
};
