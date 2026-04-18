// Fuel item profile — attach to UFlecsEntityDefinition::FuelProfile to turn an
// item into crafting fuel. Populates FCraftingFuelItemData on the prefab.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsCraftingTypes.h"
#include "FlecsFuelProfile.generated.h"

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class FATUMGAME_API UFlecsFuelProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Which fuel category this item satisfies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fuel")
	EFuelType FuelType = EFuelType::Coal;

	/** How many seconds of fuel reservoir each unit (stack count 1) provides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fuel", meta = (ClampMin = "0.1", ClampMax = "3600"))
	float ChargeSecondsPerUnit = 30.f;
};
