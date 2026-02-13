// UFlecsInventorySlotWidget: grid cell with drag-drop + highlight.

#include "FlecsInventorySlotWidget.h"
#include "FlecsContainerGridWidget.h"
#include "FlecsInventoryDragPayload.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"

void UFlecsInventorySlotWidget::BuildDefaultWidgetTree()
{
	SlotBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SlotBorder"));
	SlotBorder->SetBrushColor(DefaultColor);
	WidgetTree->RootWidget = SlotBorder;
}

void UFlecsInventorySlotWidget::InitSlot(int32 InGridX, int32 InGridY, UFlecsContainerGridWidget* InParentGrid)
{
	GridX = InGridX;
	GridY = InGridY;
	ParentGrid = InParentGrid;
}

bool UFlecsInventorySlotWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	OnDragHighlightClear();

	UFlecsInventoryDragOperation* DragOp = Cast<UFlecsInventoryDragOperation>(InOperation);
	if (!DragOp || !ParentGrid) return false;

	const FIntPoint DropPosition(GridX, GridY);

	// Cross-container drop?
	if (DragOp->SourceContainerEntityId != 0 &&
		DragOp->SourceContainerEntityId != ParentGrid->GetContainerEntityId())
	{
		if (ParentGrid->OnCrossContainerDrop)
		{
			ParentGrid->OnCrossContainerDrop(DragOp->SourceContainerEntityId, DragOp->ItemEntityId, DropPosition);
			return true;
		}
		return false;
	}

	// Same-container drop
	if (ParentGrid->CanFitAt(DragOp->ItemEntityId, DropPosition))
	{
		ParentGrid->RequestMoveItem(DragOp->ItemEntityId, DropPosition);
		return true;
	}

	return false;
}

void UFlecsInventorySlotWidget::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	UFlecsInventoryDragOperation* DragOp = Cast<UFlecsInventoryDragOperation>(InOperation);
	if (!DragOp || !ParentGrid) return;

	const bool bCross = DragOp->SourceContainerEntityId != 0 &&
		DragOp->SourceContainerEntityId != ParentGrid->GetContainerEntityId();

	const bool bCanPlace = bCross
		? ParentGrid->CanFitSizeAt(FIntPoint(GridX, GridY), DragOp->GridSize)
		: ParentGrid->CanFitAt(DragOp->ItemEntityId, FIntPoint(GridX, GridY));

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
