// UFlecsActionRouter — FPS-appropriate CommonUI input routing.

#include "FlecsActionRouter.h"
#include "Input/UIActionBindingHandle.h"

void UFlecsActionRouter::ApplyUIInputConfig(const FUIInputConfig& NewConfig, bool bForceRefresh)
{
	// Detect the engine default config: All + NoCapture + no ignore flags.
	// This fires when NO activatable widgets provide a config (= gameplay state).
	// Replace with FPS defaults: Game mode, permanent capture, locked cursor.
	if (NewConfig.GetInputMode() == ECommonInputMode::All
		&& NewConfig.GetMouseCaptureMode() == EMouseCaptureMode::NoCapture
		&& !NewConfig.bIgnoreLookInput
		&& !NewConfig.bIgnoreMoveInput)
	{
		FUIInputConfig FPSConfig(
			ECommonInputMode::Game,
			EMouseCaptureMode::CapturePermanently,
			EMouseLockMode::LockAlways);

		Super::ApplyUIInputConfig(FPSConfig, bForceRefresh);
		return;
	}

	// Panel-provided config — pass through unchanged.
	Super::ApplyUIInputConfig(NewConfig, bForceRefresh);
}
