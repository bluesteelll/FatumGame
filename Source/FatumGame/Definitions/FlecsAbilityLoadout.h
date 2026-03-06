// Data Asset — ability loadout for a character.
// Array index = slot index in FAbilitySystem.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsAbilityLoadout.generated.h"

class UFlecsAbilityDefinition;

UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsAbilityLoadout : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Abilities", meta = (TitleProperty = "AbilityName"))
	TArray<TObjectPtr<UFlecsAbilityDefinition>> Abilities;
};
