// Quick-load device profile — makes an item a stripper clip or speedloader.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsQuickLoadProfile.generated.h"

class UFlecsAmmoTypeDefinition;

UENUM(BlueprintType)
enum class EQuickLoadDeviceTypeUI : uint8
{
	StripperClip UMETA(DisplayName = "Stripper Clip"),
	Speedloader  UMETA(DisplayName = "Speedloader")
};

/**
 * Profile for quick-load devices (stripper clips, speedloaders).
 * Attach to UFlecsEntityDefinition to make an item a quick-load device.
 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class FATUMGAME_API UFlecsQuickLoadProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Type of quick-load device. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Load")
	EQuickLoadDeviceTypeUI DeviceType = EQuickLoadDeviceTypeUI::StripperClip;

	/** Number of rounds this device loads per use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Load", meta = (ClampMin = "1", ClampMax = "30"))
	int32 RoundsHeld = 5;

	/** Caliber name — must match a caliber in the CaliberRegistry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Load", meta = (GetOptions = "GetCaliberOptions"))
	FName Caliber;

	/** Ammo type that this device is pre-loaded with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Load")
	TObjectPtr<UFlecsAmmoTypeDefinition> AmmoTypeDefinition;

	/** Time in seconds for the batch insert action. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Load", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float InsertTime = 0.8f;

	/** If true, magazine must be completely empty to use this device (speedloaders). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quick Load")
	bool bRequiresEmptyMagazine = false;

	UFUNCTION()
	TArray<FName> GetCaliberOptions() const;
};
