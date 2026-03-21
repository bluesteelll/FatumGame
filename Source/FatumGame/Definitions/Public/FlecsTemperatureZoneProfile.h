// Temperature zone profile for Flecs entity spawning.
// Defines an AABB region where ambient temperature is modified.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsTemperatureZoneProfile.generated.h"

/**
 * Data Asset defining a temperature zone for the vitals system.
 *
 * Temperature zones are pure Flecs entities (no physics body) spawned via AFlecsEntitySpawner.
 * When a character is inside the zone AABB, their warmth target is modified
 * by the Temperature value.
 *
 * The zone is an axis-aligned bounding box centered on the spawner position,
 * with half-extents defined by Extent.
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsTemperatureZoneProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Temperature value (0.0 = freezing, 1.0 = warm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature Zone", meta = (ClampMin = "0", ClampMax = "1"))
	float Temperature = 0.5f;

	/** Half-extents of the zone AABB (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature Zone", meta = (ClampMin = "1"))
	FVector Extent = FVector(200.f, 200.f, 200.f);
};
