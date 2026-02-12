// UFlecsUIPanel — base class for Flecs UI panels (inventory, crafting, shop).
//
// Inherits UCommonActivatableWidget for engine-native input/cursor management.
// Sub-widgets (slots, items) use UFlecsUIWidget instead — no activation overhead.
//
// Provides the same dual-mode pattern as UFlecsUIWidget:
//   1. Pure C++ (default): BuildDefaultWidgetTree() auto-creates widget tree.
//   2. Blueprint Designer: Create BP child, design in UMG Designer.
//      BindWidgetOptional links named widgets. C++ skips auto-creation.
//
// Input handling (via UFlecsActionRouter + CommonUI pipeline):
//   - GetDesiredInputConfig() → ActionRouter manages cursor, capture, look/move.
//     Default: All + NoCapture + bIgnoreLookInput. Override for custom behavior.
//   - NativeOnActivated/Deactivated switch mapping contexts (gameplay ↔ panel).
//   - Set GameplayMappingContext + PanelMappingContext before ActivateWidget().

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "FlecsUIPanel.generated.h"

class UInputMappingContext;

UCLASS(Abstract, Blueprintable)
class FLECSUI_API UFlecsUIPanel : public UCommonActivatableWidget
{
	GENERATED_BODY()

protected:
	// ═══════════════════════════════════════════════════════════════
	// LIFECYCLE — subclasses should NOT override Initialize().
	// Use BuildDefaultWidgetTree() and PostInitialize() instead.
	// ═══════════════════════════════════════════════════════════════

	virtual bool Initialize() override;

	/** Build C++ fallback widget tree. Called ONLY when no Blueprint Designer content exists.
	 *  Use WidgetTree->ConstructWidget<T>() here. Set WidgetTree->RootWidget. */
	virtual void BuildDefaultWidgetTree() {}

	/** Called after Initialize() completes (both C++ and BP modes).
	 *  Use for non-visual setup (default widget classes, data binding, etc.). */
	virtual void PostInitialize() {}

	// ═══════════════════════════════════════════════════════════════
	// VISUAL UPDATE — call RefreshVisuals() after data changes.
	// Override OnUpdateVisuals in C++ or BP for custom rendering.
	// ═══════════════════════════════════════════════════════════════

	/** Trigger visual refresh. Calls OnUpdateVisuals(). Safe to call multiple times. */
	UFUNCTION(BlueprintCallable, Category = "FlecsUI")
	void RefreshVisuals();

	/** Override in C++ (_Implementation) or BP for custom rendering.
	 *  Called by RefreshVisuals(). All data fields should be set before this is called. */
	UFUNCTION(BlueprintNativeEvent, Category = "FlecsUI")
	void OnUpdateVisuals();
	virtual void OnUpdateVisuals_Implementation() {}

	// ═══════════════════════════════════════════════════════════════
	// INPUT CONFIG — ActionRouter reads this to manage cursor, capture, look/move.
	// Default: All + NoCapture + bIgnoreLookInput for panels.
	// Override in subclass for custom behavior (e.g., crafting panel allows movement).
	// ═══════════════════════════════════════════════════════════════

	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

	// ═══════════════════════════════════════════════════════════════
	// ACTIVATION — auto-switched on Activate/Deactivate.
	// ActionRouter handles cursor/capture/look on ACTIVATE.
	// Deactivate: manual FPS restore (ActionRouter doesn't reset without ActionDomains).
	// ═══════════════════════════════════════════════════════════════

	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;

public:
	// ═══════════════════════════════════════════════════════════════
	// MAPPING CONTEXTS — set by owning character before ActivateWidget().
	// ═══════════════════════════════════════════════════════════════

	/** Gameplay mapping context to REMOVE when panel opens (e.g. IMC_Gameplay). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlecsUI|Input")
	TObjectPtr<UInputMappingContext> GameplayMappingContext;

	/** Panel-specific mapping context to ADD when panel opens (e.g. IMC_Inventory). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FlecsUI|Input")
	TObjectPtr<UInputMappingContext> PanelMappingContext;
};
