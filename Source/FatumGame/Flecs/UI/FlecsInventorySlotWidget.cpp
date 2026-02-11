// UFlecsInventorySlotWidget: grid cell with drag-drop + highlight, fully C++.

#include "FlecsInventorySlotWidget.h"
#include "FlecsInventoryWidget.h"
#include "FlecsInventoryDragPayload.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"

namespace
{
	const FLinearColor SlotDefaultColor(0.08f, 0.08f, 0.08f, 0.6f);
	const FLinearColor SlotCanPlaceColor(0.1f, 0.6f, 0.1f, 0.6f);
	const FLinearColor SlotCannotPlaceColor(0.6f, 0.1f, 0.1f, 0.6f);
}

bool UFlecsInventorySlotWidget::Initialize()
{
	if (!Super::Initialize()) return false;

	if (WidgetTree && !WidgetTree->RootWidget)
	{
		SlotBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SlotBorder"));
		SlotBorder->SetBrushColor(SlotDefaultColor);
		WidgetTree->RootWidget = SlotBorder;
	}

	return true;
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
	ClearDragHighlight();

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
	SetDragHighlight(bCanPlace);
}

void UFlecsInventorySlotWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	ClearDragHighlight();
}

void UFlecsInventorySlotWidget::SetDragHighlight(bool bCanPlace)
{
	if (SlotBorder)
	{
		SlotBorder->SetBrushColor(bCanPlace ? SlotCanPlaceColor : SlotCannotPlaceColor);
	}
}

void UFlecsInventorySlotWidget::ClearDragHighlight()
{
	if (SlotBorder)
	{
		SlotBorder->SetBrushColor(SlotDefaultColor);
	}
}
