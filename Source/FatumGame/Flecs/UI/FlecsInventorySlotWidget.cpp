// UFlecsInventorySlotWidget: grid cell with drag-drop + highlight.

#include "FlecsInventorySlotWidget.h"
#include "FlecsInventoryWidget.h"
#include "FlecsInventoryDragPayload.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"

void UFlecsInventorySlotWidget::BuildDefaultWidgetTree()
{
	SlotBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SlotBorder"));
	SlotBorder->SetBrushColor(DefaultColor);
	WidgetTree->RootWidget = SlotBorder;
}

void UFlecsInventorySlotWidget::InitSlot(int32 InGridX, int32 InGridY, UFlecsInventoryWidget* InInventoryWidget)
{
	GridX = InGridX;
	GridY = InGridY;
	ParentInventory = InInventoryWidget;
}

bool UFlecsInventorySlotWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	OnDragHighlightClear();

	UFlecsInventoryDragOperation* DragOp = Cast<UFlecsInventoryDragOperation>(InOperation);
	if (!DragOp || !ParentInventory) return false;

	FIntPoint DropPosition(GridX, GridY);
	if (ParentInventory->CanFitAt(DragOp->ItemEntityId, DropPosition))
	{
		ParentInventory->RequestMoveItem(DragOp->ItemEntityId, DropPosition);
		return true;
	}

	return false;
}

void UFlecsInventorySlotWidget::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	UFlecsInventoryDragOperation* DragOp = Cast<UFlecsInventoryDragOperation>(InOperation);
	if (!DragOp || !ParentInventory) return;

	bool bCanPlace = ParentInventory->CanFitAt(DragOp->ItemEntityId, FIntPoint(GridX, GridY));
	OnDragHighlight(bCanPlace);
}

void UFlecsInventorySlotWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	OnDragHighlightClear();
}

// ═══════════════════════════════════════════════════════════════
// DEFAULT HIGHLIGHT — BlueprintNativeEvent implementations
// BP subclass can override these for custom visuals (glow, particles, etc.)
// ═══════════════════════════════════════════════════════════════

void UFlecsInventorySlotWidget::OnDragHighlight_Implementation(bool bCanPlace)
{
	if (SlotBorder)
	{
		SlotBorder->SetBrushColor(bCanPlace ? CanPlaceColor : CannotPlaceColor);
	}
}

void UFlecsInventorySlotWidget::OnDragHighlightClear_Implementation()
{
	if (SlotBorder)
	{
		SlotBorder->SetBrushColor(DefaultColor);
	}
}
