// UFlecsUIPanel — base class for Flecs UI panels.

#include "FlecsUIPanel.h"
#include "Blueprint/WidgetTree.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "GameFramework/PlayerController.h"

// ═══════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════

bool UFlecsUIPanel::Initialize()
{
	if (!Super::Initialize()) return false;

	// Dual-mode: if BP Designer has no root widget, build C++ fallback.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultWidgetTree();
	}

	PostInitialize();
	return true;
}

void UFlecsUIPanel::RefreshVisuals()
{
	OnUpdateVisuals();
}

// ═══════════════════════════════════════════════════════════════
// INPUT CONFIG — UFlecsActionRouter reads this via CommonUI pipeline.
// Cursor, capture, look/move ignore are all handled by the ActionRouter.
// ═══════════════════════════════════════════════════════════════

TOptional<FUIInputConfig> UFlecsUIPanel::GetDesiredInputConfig() const
{
	// All mode: Enhanced Input still works (I key toggle via PanelMappingContext).
	// NoCapture: cursor visible and free for drag-drop.
	// bIgnoreLookInput: blocks camera rotation while panel open.
	FUIInputConfig Config(ECommonInputMode::All, EMouseCaptureMode::NoCapture);
	Config.bIgnoreLookInput = true;
	return Config;
}

// ═══════════════════════════════════════════════════════════════
// ACTIVATION / DEACTIVATION
// Only mapping context switching — ActionRouter handles cursor/capture/look.
// bIgnoreAllPressedKeysUntilRelease prevents ghost inputs.
// ═══════════════════════════════════════════════════════════════

void UFlecsUIPanel::NativeOnActivated()
{
	Super::NativeOnActivated();

	APlayerController* PC = GetOwningPlayer();
	if (!PC) return;

	// Ensure panel input state even if ActionRouter skips ApplyUIInputConfig
	// (ActiveInputConfig may match from previous activation → router sees no change).
	PC->SetShowMouseCursor(true);
	PC->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));
	PC->SetIgnoreLookInput(true);

	// Mapping context swap: remove gameplay (WASD/Fire/etc), add panel (I key only)
	auto* InputSub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
	if (!InputSub) return;

	FModifyContextOptions Opts;
	Opts.bIgnoreAllPressedKeysUntilRelease = true;

	if (GameplayMappingContext)
	{
		InputSub->RemoveMappingContext(GameplayMappingContext, Opts);
	}
	if (PanelMappingContext)
	{
		InputSub->AddMappingContext(PanelMappingContext, 1, Opts);
	}
}

void UFlecsUIPanel::NativeOnDeactivated()
{
	APlayerController* PC = GetOwningPlayer();
	if (PC)
	{
		// Restore mapping contexts
		auto* InputSub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
		if (InputSub)
		{
			FModifyContextOptions Opts;
			Opts.bIgnoreAllPressedKeysUntilRelease = true;

			if (PanelMappingContext)
			{
				InputSub->RemoveMappingContext(PanelMappingContext, Opts);
			}
			if (GameplayMappingContext)
			{
				InputSub->AddMappingContext(GameplayMappingContext, 0, Opts);
			}
		}

		// Restore FPS input state explicitly.
		// CommonUI's ActionRouter does NOT reset input config on deactivation
		// without ActionDomainTable (RefreshActionDomainLeafNodeConfig exits early).
		// On next ActivateWidget(), the router WILL apply the panel config, overriding these.
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
		PC->ResetIgnoreLookInput();
	}

	Super::NativeOnDeactivated();
}
