// Vitals item profile for Flecs item spawning.
// Defines one-shot consumption effects and passive inventory bonuses.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsVitalsItemProfile.generated.h"

/**
 * Data Asset defining vitals effects for an item.
 *
 * Items with VitalsItemProfile can restore hunger/thirst/warmth on use
 * and/or provide passive bonuses while sitting in the player's inventory.
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsVitalsItemProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// CONSUMPTION (one-shot restoration on use)
	// ═══════════════════════════════════════════════════════════════

	/** Hunger restored on consumption (0.0–1.0 of max) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumption", meta = (ClampMin = "0", ClampMax = "1"))
	float HungerRestore = 0.f;

	/** Thirst restored on consumption (0.0–1.0 of max) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumption", meta = (ClampMin = "0", ClampMax = "1"))
	float ThirstRestore = 0.f;

	/** Warmth restored on consumption (0.0–1.0 of max) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumption", meta = (ClampMin = "0", ClampMax = "1"))
	float WarmthRestore = 0.f;

	// ═══════════════════════════════════════════════════════════════
	// PASSIVE (while in inventory)
	// ═══════════════════════════════════════════════════════════════

	/** Passive warmth bonus while in inventory (0.0–1.0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passive", meta = (ClampMin = "0", ClampMax = "1"))
	float PassiveWarmthBonus = 0.f;

	/** Passive hunger drain multiplier (< 1.0 = slower drain) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passive", meta = (ClampMin = "0", ClampMax = "2"))
	float PassiveHungerDrainMultiplier = 1.f;

	/** Passive thirst drain multiplier (< 1.0 = slower drain) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passive", meta = (ClampMin = "0", ClampMax = "2"))
	float PassiveThirstDrainMultiplier = 1.f;
};
