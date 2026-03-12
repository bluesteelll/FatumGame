// Noise zone profile for Flecs entity spawning.
// Defines an AABB region where footstep noise is modified by surface type.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsStealthTypes.h"
#include "FlecsNoiseZoneProfile.generated.h"

/**
 * Data Asset defining a noise zone for the stealth system.
 *
 * Noise zones are pure Flecs entities (no physics body) spawned via AFlecsEntitySpawner.
 * When a character walks inside the zone AABB, their footstep noise is modified
 * by the SurfaceType multiplier.
 *
 * The zone is an axis-aligned bounding box centered on the spawner position,
 * with half-extents defined by Extent.
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsNoiseZoneProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Surface type — determines the noise multiplier for footsteps */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise Zone")
	ESurfaceNoise SurfaceType = ESurfaceNoise::Normal;

	/** Half-extents of the zone AABB (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise Zone", meta = (ClampMin = "1"))
	FVector Extent = FVector(200.f, 200.f, 100.f);
};
