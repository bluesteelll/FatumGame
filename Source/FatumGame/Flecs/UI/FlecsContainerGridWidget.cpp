// UFlecsContainerGridWidget: reusable grid with drag-drop, extracted from UFlecsInventoryWidget.

#include "FlecsContainerGridWidget.h"
#include "FlecsContainerModel.h"
#include "FlecsInventorySlotWidget.h"
#include "FlecsInventoryItemWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"

// ═══════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════

void UFlecsContainerGridWidget::BuildDefaultWidgetTree()
{
	BackgroundBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackgroundBorder"));
	BackgroundBorder->SetBrushColor(BackgroundColor);
	BackgroundBorder->SetPadding(FMargin(BackgroundPadding));

	GridCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("GridCanvas"));
	BackgroundBorder->SetContent(GridCanvas);

	WidgetTree->RootWidget = BackgroundBorder;
}

void UFlecsContainerGridWidget::PostInitialize()
{
	if (!SlotWidgetClass) SlotWidgetClass = UFlecsInventorySlotWidget::StaticClass();
	if (!ItemWidgetClass) ItemWidgetClass = UFlecsInventoryItemWidget::StaticClass();
}

// ═══════════════════════════════════════════════════════════════
// BIND / UNBIND
// ═══════════════════════════════════════════════════════════════

void UFlecsContainerGridWidget::BindContainer(UFlecsContainerModel* InModel, int64 InContainerEntityId)
{
	check(InModel);
	check(InContainerEntityId != 0);

	ContainerModel = InModel;
	ContainerEntityId = InContainerEntityId;
	ContainerModel->BindView(this);

	// If model already has data, render immediately
	if (ContainerModel->GetGridWidth() > 0)
	{
		BuildGrid(ContainerModel->GetGridWidth(), ContainerModel->GetGridHeight());
		PopulateItems(ContainerModel->GetItems());
	}
}

void UFlecsContainerGridWidget::UnbindContainer()
{
	if (ContainerModel)
	{
		ContainerModel->UnbindView(this);
		ContainerModel = nullptr;
	}
	ClearAll();
	ContainerEntityId = 0;
}

// ═══════════════════════════════════════════════════════════════
// API (used by slot/item widgets)
// ═══════════════════════════════════════════════════════════════

void UFlecsContainerGridWidget::RequestMoveItem(int64 ItemEntityId, FIntPoint NewGridPosition)
{
	if (!ContainerModel || ItemEntityId == 0) return;
	ContainerModel->MoveItem(ItemEntityId, NewGridPosition);
}

bool UFlecsContainerGridWidget::CanFitAt(int64 ItemEntityId, FIntPoint Position) const
{
	if (!ContainerModel) return false;
	return ContainerModel->CanFitAt(ItemEntityId, Position);
}

bool UFlecsContainerGridWidget::CanStackAt(int64 ItemEntityId, FIntPoint Position) const
{
	if (!ContainerModel) return false;
	return ContainerModel->CanStackAt(ItemEntityId, Position);
}

bool UFlecsContainerGridWidget::CanFitSizeAt(FIntPoint Position, FIntPoint Size) const
{
	if (!ContainerModel) return false;
	return ContainerModel->CanFitSizeAt(Position, Size);
}

// ═══════════════════════════════════════════════════════════════
// IFlecsContainerView
// ═══════════════════════════════════════════════════════════════

void UFlecsContainerGridWidget::OnContainerSnapshotReceived(const FContainerSnapshot& Snapshot)
{
	if (!ContainerModel || Snapshot.ContainerEntityId != ContainerEntityId) return;

	if (SlotWidgets.Num() != Snapshot.GridWidth * Snapshot.GridHeight)
	{
		BuildGrid(Snapshot.GridWidth, Snapshot.GridHeight);
	}

	PopulateItems(Snapshot.Items);
}

void UFlecsContainerGridWidget::OnItemMoved(int64 ItemEntityId, FIntPoint OldPos, FIntPoint NewPos)
{
	if (!ContainerModel) return;
	PopulateItems(ContainerModel->GetItems());
}

void UFlecsContainerGridWidget::OnOperationRolledBack(uint32 OpId)
{
	if (!ContainerModel) return;
	PopulateItems(ContainerModel->GetItems());
}

// ═══════════════════════════════════════════════════════════════
// GRID BUILDING
// ═══════════════════════════════════════════════════════════════

void UFlecsContainerGridWidget::BuildGrid(int32 InGridWidth, int32 InGridHeight)
{
	ClearAll();
	if (!GridCanvas) return;

	for (int32 Y = 0; Y < InGridHeight; ++Y)
	{
		for (int32 X = 0; X < InGridWidth; ++X)
		{
			UFlecsInventorySlotWidget* SlotWidget = CreateWidget<UFlecsInventorySlotWidget>(GetOwningPlayer(), SlotWidgetClass);
			check(SlotWidget);
			SlotWidget->InitSlot(X, Y, this);

			UCanvasPanelSlot* CanvasSlot = GridCanvas->AddChildToCanvas(SlotWidget);
			CanvasSlot->SetPosition(FVector2D(X * CellSize, Y * CellSize));
			CanvasSlot->SetSize(FVector2D(CellSize, CellSize));
			CanvasSlot->SetAutoSize(false);

			SlotWidgets.Add(SlotWidget);
		}
	}
}

void UFlecsContainerGridWidget::PopulateItems(const TArray<FContainerItemSnapshot>& Items)
{
	for (UFlecsInventoryItemWidget* W : ItemWidgets)
	{
		if (W) W->RemoveFromParent();
	}
	ItemWidgets.Empty();

	if (!GridCanvas) return;

	for (const FContainerItemSnapshot& Item : Items)
	{
		if (Item.GridPosition.X < 0) continue;

		UFlecsInventoryItemWidget* ItemWidget = CreateWidget<UFlecsInventoryItemWidget>(GetOwningPlayer(), ItemWidgetClass);
		check(ItemWidget);
		ItemWidget->SetItemData(Item);
		ItemWidget->SetParentGrid(this);

		UCanvasPanelSlot* CanvasSlot = GridCanvas->AddChildToCanvas(ItemWidget);
		CanvasSlot->SetPosition(FVector2D(Item.GridPosition.X * CellSize, Item.GridPosition.Y * CellSize));
		CanvasSlot->SetSize(FVector2D(Item.GridSize.X * CellSize, Item.GridSize.Y * CellSize));
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetZOrder(1);

		ItemWidgets.Add(ItemWidget);
	}
}

void UFlecsContainerGridWidget::ClearAll()
{
	for (UFlecsInventorySlotWidget* W : SlotWidgets)
	{
		if (W) W->RemoveFromParent();
	}
	SlotWidgets.Empty();

	for (UFlecsInventoryItemWidget* W : ItemWidgets)
	{
		if (W) W->RemoveFromParent();
	}
	ItemWidgets.Empty();
}
