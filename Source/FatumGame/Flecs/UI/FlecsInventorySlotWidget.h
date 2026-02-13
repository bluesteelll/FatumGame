// Single grid cell in the inventory. Handles drag-drop target + visual highlight.
// Inherits UFlecsUIWidget for automatic C++/BP Designer dual-mode.
//
// Two usage modes:
//   1. Pure C++ (default): BuildDefaultWidgetTree() auto-creates a Border.
//   2. Blueprint Designer: Create WBP_Slot child, add Border ("SlotBorder").
//      Customize materials, textures, animations in Designer.

#pragma once

#include "CoreMinimal.h"
#include "FlecsUIWidget.h"
#include "FlecsInventorySlotWidget.generated.h"

class UBorder;
class UFlecsContainerGridWidget;

UCLASS()
class FATUMGAME_API UFlecsInventorySlotWidget : public UFlecsUIWidget
{
	GENERATED_BODY()

public:
	void InitSlot(int32 InGridX, int32 InGridY, UFlecsContainerGridWidget* InParentGrid);

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	int32 GridX = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	int32 GridY = 0;

protected:
	virtual void BuildDefaultWidgetTree() override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;
	virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;

	// ═══════════════════════════════════════════════════════════════
	// VIRTUAL — override in BP subclass for custom highlight logic
	// ═══════════════════════════════════════════════════════════════

	/** Called when dragged item enters this slot. Override for custom highlight (particles, glow, etc.). */
	UFUNCTION(BlueprintNativeEvent, Category = "Inventory|Slot")
	void OnDragHighlight(bool bCanPlace);

	/** Called when dragged item leaves this slot. Override to clear custom highlight. */
	UFUNCTION(BlueprintNativeEvent, Category = "Inventory|Slot")
	void OnDragHighlightClear();

	// ═══════════════════════════════════════════════════════════════
	// DESIGNER-BINDABLE + VISUAL CONFIG
	// ═══════════════════════════════════════════════════════════════

	/** Border widget. Name in Designer: "SlotBorder". */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> SlotBorder;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Slot|Colors")
	FLinearColor DefaultColor = FLinearColor(0.08f, 0.08f, 0.08f, 0.6f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Slot|Colors")
	FLinearColor CanPlaceColor = FLinearColor(0.1f, 0.6f, 0.1f, 0.6f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Slot|Colors")
	FLinearColor CannotPlaceColor = FLinearColor(0.6f, 0.1f, 0.1f, 0.6f);

private:
	UPROPERTY()
	TObjectPtr<UFlecsContainerGridWidget> ParentGrid;
};
