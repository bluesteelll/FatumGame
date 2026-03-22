// Magazine profile — defines magazine capacity, caliber, and accepted ammo types.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsMagazineProfile.generated.h"

class UFlecsAmmoTypeDefinition;

UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsMagazineProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Caliber — select from CaliberRegistry (Project Settings → Fatum Game). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magazine", meta = (GetOptions = "GetCaliberOptions"))
	FName Caliber;

	/** Maximum round capacity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magazine", meta = (ClampMin = "1", ClampMax = "100"))
	int32 Capacity = 30;

	/** Accepted ammo types (max 8). Only these can be loaded into this magazine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magazine", meta = (TitleProperty = "AmmoName"))
	TArray<TObjectPtr<UFlecsAmmoTypeDefinition>> AcceptedAmmoTypes;

	/** Weight per round in kg (for dynamic inventory weight) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magazine", meta = (ClampMin = "0"))
	float WeightPerRound = 0.011f;

	/** Reload speed modifier. Drum mags = 1.3 (slower), speed mags = 0.8 (faster). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magazine", meta = (ClampMin = "0.3", ClampMax = "3.0"))
	float ReloadSpeedModifier = 1.0f;

	/** Default ammo type to pre-fill magazines with on spawn. nullptr = spawn empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magazine")
	TObjectPtr<UFlecsAmmoTypeDefinition> DefaultAmmoType;

	UFUNCTION()
	TArray<FName> GetCaliberOptions() const;
};
