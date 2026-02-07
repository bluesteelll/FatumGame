// Interaction profile for Flecs entity spawning.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsInteractionProfile.generated.h"

/**
 * Data Asset defining interaction properties for entity spawning.
 *
 * Used with FEntitySpawnRequest to make an entity interactable.
 * The interaction type is determined at runtime from entity tags
 * (FTagPickupable + FTagItem → pickup, FTagContainer → open, else → generic use).
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsInteractionProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Text shown to the player when looking at this entity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	FText InteractionPrompt = NSLOCTEXT("Interaction", "Default", "Press E to interact");

	/** Maximum range at which this entity can be interacted with (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "50", ClampMax = "1000"))
	float InteractionRange = 300.f;

	/** If true, entity becomes non-interactable after first use */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bSingleUse = false;
};
