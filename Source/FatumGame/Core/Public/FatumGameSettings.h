// Project-wide settings for FatumGame.
// Accessible via Project Settings → Game → Fatum Game.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "FatumGameSettings.generated.h"

class UFlecsCaliberRegistry;

/**
 * Global FatumGame settings (Project Settings → Game → Fatum Game).
 * One instance per project, saved in DefaultGame.ini.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Fatum Game"))
class FATUMGAME_API UFatumGameSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Caliber registry — defines all caliber types in the project.
	 *  Create a FlecsCaliberRegistry DataAsset and assign it here. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Weapon System")
	TSoftObjectPtr<UFlecsCaliberRegistry> CaliberRegistry;

	/** Get the loaded caliber registry (loads if needed). */
	UFlecsCaliberRegistry* GetCaliberRegistry() const
	{
		return CaliberRegistry.LoadSynchronous();
	}

	/** Convenience: get the singleton settings instance. */
	static const UFatumGameSettings* Get()
	{
		return GetDefault<UFatumGameSettings>();
	}
};
