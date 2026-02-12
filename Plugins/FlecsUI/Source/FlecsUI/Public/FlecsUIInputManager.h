// Centralized UI input mode management.
// Manages a stack of active UI panels — the topmost panel determines input state.
// Handles mapping context switching, cursor visibility, and input mode.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FlecsUIInputManager.generated.h"

class UFlecsUIInputConfig;
class UInputMappingContext;

UCLASS()
class FLECSUI_API UFlecsUIInputManager : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// ═══ Configuration (call once on startup) ═══

	/** Configure base mapping contexts. GameplayContext = normal play, UIContext = base UI (e.g. toggle key). */
	void Configure(UInputMappingContext* InGameplayContext, UInputMappingContext* InUIContext);

	// ═══ Panel Lifecycle ═══

	/** Push a UI panel onto the stack. Token identifies the panel (widget, model, etc.). */
	void PushPanel(UObject* PanelToken, UFlecsUIInputConfig* Config);

	/** Pop a specific panel from the stack (doesn't have to be top). */
	void PopPanel(UObject* PanelToken);

	/** Check if any UI panel is active. */
	bool HasActivePanel() const { return PanelStack.Num() > 0; }

	/** Get the number of active panels. */
	int32 GetActivePanelCount() const { return PanelStack.Num(); }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Recalculate input state from the topmost panel (or restore gameplay if empty). */
	void ApplyInputState();

	struct FUIPanel
	{
		TWeakObjectPtr<UObject> Token;
		TObjectPtr<UFlecsUIInputConfig> Config;
	};

	/** LIFO stack of active panels. Top determines input state. */
	TArray<FUIPanel> PanelStack;

	/** Base mapping contexts set via Configure(). */
	TObjectPtr<UInputMappingContext> GameplayContext;
	TObjectPtr<UInputMappingContext> UIContext;

	/** Currently applied panel-specific context (to remove on switch). */
	TObjectPtr<UInputMappingContext> ActivePanelContext;
};
