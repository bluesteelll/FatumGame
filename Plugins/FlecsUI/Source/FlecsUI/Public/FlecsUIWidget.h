// UFlecsUIWidget — base class for all Flecs-connected UMG widgets.
//
// Provides:
//   1. Automatic C++ / Blueprint Designer dual-mode:
//      - Override BuildDefaultWidgetTree() for C++ fallback visuals.
//      - If BP child has Designer content, BuildDefaultWidgetTree() is NOT called.
//      - BindWidgetOptional links named widgets from Designer automatically.
//
//   2. OnUpdateVisuals() — BlueprintNativeEvent called after data changes.
//      Override in C++ (_Implementation) or BP (Event Graph) for custom rendering.
//
//   3. All subclasses are Blueprintable by default.
//
// Usage pattern for new widgets:
//
//   UCLASS()
//   class UMyWidget : public UFlecsUIWidget
//   {
//       GENERATED_BODY()
//   protected:
//       // BindWidgetOptional: BP Designer provides, or BuildDefaultWidgetTree creates
//       UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
//       TObjectPtr<UBorder> MyBorder;
//
//       // Editable visual properties
//       UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MyWidget")
//       FLinearColor MyColor = FLinearColor::White;
//
//       virtual void BuildDefaultWidgetTree() override
//       {
//           MyBorder = WidgetTree->ConstructWidget<UBorder>(...);
//           WidgetTree->RootWidget = MyBorder;
//       }
//
//       virtual void OnUpdateVisuals_Implementation() override
//       {
//           MyBorder->SetBrushColor(MyColor);
//       }
//   };

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlecsUIWidget.generated.h"

UCLASS(Abstract, Blueprintable)
class FLECSUI_API UFlecsUIWidget : public UUserWidget
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
};
