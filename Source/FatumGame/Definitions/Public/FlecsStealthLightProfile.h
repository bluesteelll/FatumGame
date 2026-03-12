// Stealth light profile for Flecs entity spawning.
// Defines a gameplay-only light source for stealth illumination computation.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsStealthTypes.h"
#include "FlecsStealthLightProfile.generated.h"

/**
 * Data Asset defining a stealth light source.
 *
 * These are NOT rendering lights — they are pure gameplay entities used by the
 * stealth system to compute character LightLevel. Spawn via AFlecsEntitySpawner
 * with this profile set on a UFlecsEntityDefinition.
 *
 * Point lights illuminate in all directions within Radius.
 * Spot lights illuminate within a cone defined by inner/outer angles.
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsStealthLightProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Point or Spot light */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	EStealthLightType LightType = EStealthLightType::Point;

	/** Light intensity [0, 1] — contribution when fully illuminated */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0", ClampMax = "1"))
	float Intensity = 1.f;

	/** Maximum illumination radius (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0"))
	float Radius = 1000.f;

	/** Inner cone angle in degrees (full intensity). Spot light only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spot Light",
		meta = (EditCondition = "LightType == EStealthLightType::Spot", EditConditionHides,
			ClampMin = "0", ClampMax = "90"))
	float InnerConeAngle = 20.f;

	/** Outer cone angle in degrees (falloff edge). Spot light only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spot Light",
		meta = (EditCondition = "LightType == EStealthLightType::Spot", EditConditionHides,
			ClampMin = "0", ClampMax = "90"))
	float OuterConeAngle = 40.f;
};
