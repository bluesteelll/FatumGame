// Per-panel input configuration (Data Asset).
// Each UI panel (inventory, crafting, shop) has its own config
// defining cursor behavior, input mode, and optional panel-specific mapping context.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsUIInputConfig.generated.h"

class UInputMappingContext;

UCLASS(BlueprintType)
class FLECSUI_API UFlecsUIInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Optional mapping context specific to this panel (added on top of base UI context). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> PanelMappingContext;

	/** Show mouse cursor when this panel is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	bool bShowCursor = true;

	/** Mouse lock mode when this panel is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	EMouseLockMode MouseLockMode = EMouseLockMode::DoNotLock;

	/** True = GameAndUI (allows game input too), False = UIOnly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	bool bGameAndUI = true;

	/** Hide cursor during mouse capture (drag). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	bool bHideCursorDuringCapture = false;
};
