// Editor-only noise zone actor. Visualizes the AABB in the editor,
// spawns a Flecs entity at BeginPlay, then destroys itself — zero runtime overhead.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlecsNoiseZoneActor.generated.h"

class UFlecsEntityDefinition;
class UBoxComponent;

/**
 * Place in editor to define a stealth noise zone.
 *
 * HOW TO USE:
 * 1. Drag AFlecsNoiseZoneActor into the level
 * 2. Set EntityDefinition (DA with NoiseZoneProfile — sets surface type and half-extents)
 * 3. Box resizes live as you edit the profile Extent
 * 4. Play — Flecs entity spawns and this actor destroys itself (zero runtime cost)
 */
UCLASS(Blueprintable, BlueprintType)
class FATUMGAME_API AFlecsNoiseZoneActor : public AActor
{
	GENERATED_BODY()

public:
	AFlecsNoiseZoneActor();

	/** EntityDefinition with NoiseZoneProfile set */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise Zone")
	TObjectPtr<UFlecsEntityDefinition> EntityDefinition;

	/** Editor box visualization — hidden in game, no collision */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Noise Zone")
	TObjectPtr<UBoxComponent> ZoneBox;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
};
