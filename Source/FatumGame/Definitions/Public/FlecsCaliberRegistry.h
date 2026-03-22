// Caliber registry — single DataAsset listing all calibers in the project.
// Each caliber gets an auto-assigned uint8 index for fast comparison in ECS.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsCaliberRegistry.generated.h"

/**
 * Central registry of all caliber types.
 *
 * Create ONE instance in Content Browser: Data Asset → FlecsCaliberRegistry.
 * Add all calibers (e.g., "5.56x45", "7.62x39", "12Gauge").
 * Order determines index (0, 1, 2...) used in ECS for fast matching.
 *
 * Referenced by: WeaponProfile, MagazineProfile, AmmoTypeDefinition.
 */
UCLASS(BlueprintType)
class FATUMGAME_API UFlecsCaliberRegistry : public UDataAsset
{
	GENERATED_BODY()

public:
	/** List of all caliber names. Index = CaliberId used in ECS. Max 255. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibers")
	TArray<FName> Calibers;

	/** Get the uint8 index for a caliber name. Returns 0xFF if not found. */
	uint8 GetCaliberIndex(FName CaliberName) const
	{
		int32 Idx = Calibers.IndexOfByKey(CaliberName);
		if (Idx == INDEX_NONE || Idx > 254) return 0xFF;
		return static_cast<uint8>(Idx);
	}

	/** Get caliber name by index. */
	FName GetCaliberName(uint8 Index) const
	{
		if (Index >= Calibers.Num()) return NAME_None;
		return Calibers[Index];
	}

	/** Check if caliber exists. */
	bool HasCaliber(FName CaliberName) const
	{
		return Calibers.Contains(CaliberName);
	}
};

/** Invalid caliber index sentinel */
static constexpr uint8 CALIBER_INVALID = 0xFF;
