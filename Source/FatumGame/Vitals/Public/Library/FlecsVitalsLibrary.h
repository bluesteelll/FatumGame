// Blueprint function library for vitals item consumption.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FlecsVitalsLibrary.generated.h"

UCLASS()
class FATUMGAME_API UFlecsVitalsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Consume a vitals item (food/water) from a character's inventory.
	 * Enqueues to sim thread: reads FVitalsItemStatic from item prefab,
	 * restores vitals on character, decrements item count.
	 *
	 * @param WorldContextObject World context
	 * @param ItemEntityId Flecs entity ID of the item to consume
	 * @param CharacterEntityId Flecs entity ID of the character consuming
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Vitals", meta = (WorldContext = "WorldContextObject"))
	static void ConsumeVitalItem(UObject* WorldContextObject, int64 ItemEntityId, int64 CharacterEntityId);
};
