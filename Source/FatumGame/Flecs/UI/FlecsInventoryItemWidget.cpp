// UFlecsInventoryItemWidget: item visual + drag initiation.

#include "FlecsInventoryItemWidget.h"
#include "FlecsInventoryWidget.h"
#include "FlecsInventoryDragPayload.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

void UFlecsInventoryItemWidget::BuildDefaultWidgetTree()
{
	ItemBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ItemBorder"));
	ItemBorder->SetBrushColor(ItemBackgroundColor);
	ItemBorder->SetPadding(FMargin(4.f));

	UVerticalBox* VBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("VBox"));

	TextItemName = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TextItemName"));
	TextItemName->SetColorAndOpacity(FSlateColor(ItemNameColor));
	VBox->AddChildToVerticalBox(TextItemName);

	TextCount = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TextCount"));
	TextCount->SetColorAndOpacity(FSlateColor(CountTextColor));
	VBox->AddChildToVerticalBox(TextCount);

	ItemBorder->SetContent(VBox);
	WidgetTree->RootWidget = ItemBorder;
}

void UFlecsInventoryItemWidget::SetItemData(const FContainerItemSnapshot& Data)
{
	ItemEntityId = Data.ItemEntityId;
	TypeId = Data.TypeId;
	Count = Data.Count;
	MaxStack = Data.MaxStack;
	RarityTier = Data.RarityTier;
	GridPosition = Data.GridPosition;
	GridSize = Data.GridSize;
	ItemDefinition = Data.ItemDefinition;

	// Call virtual — BP subclass can override OnUpdateVisuals for icons, rarity borders, etc.
	RefreshVisuals();
}

// ═══════════════════════════════════════════════════════════════
// DEFAULT OnUpdateVisuals — from UFlecsUIWidget base.
// Sets text blocks if available. BP override can do anything.
// ═══════════════════════════════════════════════════════════════

void UFlecsInventoryItemWidget::OnUpdateVisuals_Implementation()
{
	if (TextItemName)
	{
		TextItemName->SetText(FText::FromName(TypeId));
	}
	if (TextCount)
	{
		if (Count > 1)
		{
			TextCount->SetText(FText::FromString(FString::Printf(TEXT("x%d"), Count)));
			TextCount->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			TextCount->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

// ═══════════════════════════════════════════════════════════════
// DRAG INITIATION
// ═══════════════════════════════════════════════════════════════

FReply UFlecsInventoryItemWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}
	return FReply::Unhandled();
}

void UFlecsInventoryItemWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
	UDragDropOperation*& OutOperation)
{
	UFlecsInventoryDragOperation* DragOp = NewObject<UFlecsInventoryDragOperation>();
	DragOp->ItemEntityId = ItemEntityId;
	DragOp->OriginalPosition = GridPosition;
	DragOp->GridSize = GridSize;
	DragOp->DefaultDragVisual = this;
	DragOp->Pivot = EDragPivot::CenterCenter;

	OutOperation = DragOp;
}

// ═══════════════════════════════════════════════════════════════
// DROP FORWARDING — item widgets sit at z=1 above slots,
// so they must forward drag-drop events to the inventory.
// Without this, drops on occupied cells silently fail.
// ═══════════════════════════════════════════════════════════════

bool UFlecsInventoryItemWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	UFlecsInventoryDragOperation* DragOp = Cast<UFlecsInventoryDragOperation>(InOperation);
	if (!DragOp || !ParentInventory) return false;

	// Don't drop on yourself
	if (DragOp->ItemEntityId == ItemEntityId) return false;

	if (ParentInventory->CanFitAt(DragOp->ItemEntityId, GridPosition))
	{
		ParentInventory->RequestMoveItem(DragOp->ItemEntityId, GridPosition);
		return true;
	}

	return false;
}

void UFlecsInventoryItemWidget::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	// No highlight on item widgets — slots handle that
}

void UFlecsInventoryItemWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	// No highlight on item widgets — slots handle that
}
