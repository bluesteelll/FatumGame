// UFlecsActionRouter — Custom ActionRouter for FPS games using CommonUI.
//
// Problem: CommonUI's default input config (when no panels active) is
// All + NoCapture — cursor visible, not captured. FPS needs the opposite.
//
// Solution: Override ApplyUIInputConfig to detect the engine default and
// replace it with FPS-appropriate settings (Game + CapturePermanently).
//
// Auto-registration: UCommonUIActionRouterBase::ShouldCreateSubsystem()
// returns false when a derived class exists. No manual registration needed.

#pragma once

#include "CoreMinimal.h"
#include "Input/CommonUIActionRouterBase.h"
#include "FlecsActionRouter.generated.h"

UCLASS()
class FLECSUI_API UFlecsActionRouter : public UCommonUIActionRouterBase
{
	GENERATED_BODY()

protected:
	virtual void ApplyUIInputConfig(const FUIInputConfig& NewConfig, bool bForceRefresh) override;
};
