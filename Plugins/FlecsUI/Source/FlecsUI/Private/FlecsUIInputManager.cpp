// UFlecsUIInputManager — centralized UI input mode management.

#include "FlecsUIInputManager.h"
#include "FlecsUIInputConfig.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "GameFramework/PlayerController.h"

bool UFlecsUIInputManager::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFlecsUIInputManager::Configure(UInputMappingContext* InGameplayContext, UInputMappingContext* InUIContext)
{
	GameplayContext = InGameplayContext;
	UIContext = InUIContext;
}

void UFlecsUIInputManager::PushPanel(UObject* PanelToken, UFlecsUIInputConfig* Config)
{
	check(PanelToken);
	check(Config);

	// Avoid duplicate push
	for (const auto& Entry : PanelStack)
	{
		if (Entry.Token.Get() == PanelToken) return;
	}

	PanelStack.Add({ PanelToken, Config });
	ApplyInputState();
}

void UFlecsUIInputManager::PopPanel(UObject* PanelToken)
{
	if (!PanelToken) return;

	PanelStack.RemoveAll([PanelToken](const FUIPanel& Entry)
	{
		return !Entry.Token.IsValid() || Entry.Token.Get() == PanelToken;
	});

	ApplyInputState();
}

void UFlecsUIInputManager::ApplyInputState()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;

	UEnhancedInputLocalPlayerSubsystem* InputSub =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
	if (!InputSub) return;

	FModifyContextOptions Opts;
	Opts.bIgnoreAllPressedKeysUntilRelease = true;

	// Remove previous panel-specific context if any
	if (ActivePanelContext)
	{
		InputSub->RemoveMappingContext(ActivePanelContext, Opts);
		ActivePanelContext = nullptr;
	}

	if (PanelStack.Num() > 0)
	{
		const auto& Top = PanelStack.Last();

		// Remove gameplay context
		if (GameplayContext)
		{
			InputSub->RemoveMappingContext(GameplayContext, Opts);
		}

		// Add base UI context
		if (UIContext)
		{
			InputSub->AddMappingContext(UIContext, 1, Opts);
		}

		// Add panel-specific context if any
		if (Top.Config->PanelMappingContext)
		{
			InputSub->AddMappingContext(Top.Config->PanelMappingContext, 2, Opts);
			ActivePanelContext = Top.Config->PanelMappingContext;
		}

		// Apply cursor and input mode from config
		PC->SetShowMouseCursor(Top.Config->bShowCursor);

		if (Top.Config->bGameAndUI)
		{
			FInputModeGameAndUI Mode;
			Mode.SetLockMouseToViewportBehavior(Top.Config->MouseLockMode);
			Mode.SetHideCursorDuringCapture(Top.Config->bHideCursorDuringCapture);
			PC->SetInputMode(Mode);
		}
		else
		{
			PC->SetInputMode(FInputModeUIOnly());
		}
	}
	else
	{
		// No panels — restore gameplay
		if (UIContext)
		{
			InputSub->RemoveMappingContext(UIContext, Opts);
		}

		if (GameplayContext)
		{
			InputSub->AddMappingContext(GameplayContext, 0, Opts);
		}

		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
	}
}
