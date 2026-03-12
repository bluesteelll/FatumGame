// Stealth system enums.
// Used by stealth profiles (data assets), stealth components (ECS), and stealth systems.

#pragma once

#include "CoreMinimal.h"
#include "FlecsStealthTypes.generated.h"

/** Surface noise type for noise zones */
UENUM(BlueprintType)
enum class ESurfaceNoise : uint8
{
	Quiet    = 0 UMETA(DisplayName = "Quiet (x0.5)"),
	Normal   = 1 UMETA(DisplayName = "Normal (x1.0)"),
	Loud     = 2 UMETA(DisplayName = "Loud (x1.5)"),
	VeryLoud = 3 UMETA(DisplayName = "Very Loud (x2.0)")
};

/** Light type for stealth light entities */
UENUM(BlueprintType)
enum class EStealthLightType : uint8
{
	Point = 0,
	Spot  = 1
};
