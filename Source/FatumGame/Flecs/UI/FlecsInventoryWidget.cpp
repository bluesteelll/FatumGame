// UFlecsInventoryWidget: Grid inventory using Model/View pattern.
// All data flows through UFlecsContainerModel (lock-free triple buffer from sim thread).

#include "FlecsInventoryWidget.h"
#include "FlecsCharacter.h"
#include "FlecsContainerModel.h"
#include "FlecsUISubsystem.h"
#include "FlecsInventorySlotWidget.h"
#include "FlecsInventoryItemWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"

// ═══════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════

void UFlecsInventoryWidget::BuildDefaultWidgetTree()
{
	BackgroundBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackgroundBorder"));
	BackgroundBorder->SetBrushColor(BackgroundColor);
	BackgroundBorder->SetPadding(FMargin(BackgroundPadding));

	GridCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("GridCanvas"));
	BackgroundBorder->SetContent(GridCanvas);

	WidgetTree->RootWidget = BackgroundBorder;
}

void UFlecsInventoryWidget::PostInitialize()
{
	if (!SlotWidgetClass) SlotWidgetClass = UFlecsInventorySlotWidget::StaticClass();
	if (!ItemWidgetClass) ItemWidgetClass = UFlecsInventoryItemWidget::StaticClass();
}

// ═══════════════════════════════════════════════════════════════
// PUBLIC API
// ═══════════════════════════════════════════════════════════════

void UFlecsInventoryWidget::SetOwningCharacter(AFlecsCharacter* InCharacter)
{
	OwningCharacter = InCharacter;
}

void UFlecsInventoryWidget::OpenInventory(int64 InContainerEntityId)
{
	if (InContainerEntityId == 0) return;

	ContainerEntityId = InContainerEntityId;
	bIsOpen = true;

	// Acquire model from subsystem (creates if needed, ref-counted)
	UFlecsUISubsystem* UISub = UFlecsUISubsystem::SelfPtr;
	check(UISub);
	ContainerModel = UISub->AcquireContainerModel(ContainerEntityId);
	ContainerModel->BindView(this);

	// If model already has data from a previous open, use it immediately
	if (ContainerModel->GetGridWidth() > 0)
	{
		BuildGrid(ContainerModel->GetGridWidth(), ContainerModel->GetGridHeight());
		PopulateItems(ContainerModel->GetItems());
	}
	// Otherwise, snapshot will arrive on next Tick via OnContainerSnapshotReceived

	// CommonUI: triggers NativeOnActivated → mapping context switch + cursor
	ActivateWidget();

	UE_LOG(LogTemp, Log, TEXT("OpenInventory: ContainerId=%lld Model=%s Grid=%dx%d"),
		ContainerEntityId, *ContainerModel->GetName(),
		ContainerModel->GetGridWidth(), ContainerModel->GetGridHeight());
}

void UFlecsInventoryWidget::CloseInventory()
{
	if (!bIsOpen) return;

	// CommonUI: triggers NativeOnDeactivated → restore mapping context + cursor
	DeactivateWidget();

	bIsOpen = false;
	ClearAll();

	// Unbind and release model
	if (ContainerModel)
	{
		ContainerModel->UnbindView(this);
		if (UFlecsUISubsystem* UISub = UFlecsUISubsystem::SelfPtr)
		{
			UISub->ReleaseModel(ContainerEntityId);
		}
		ContainerModel = nullptr;
	}

	ContainerEntityId = 0;
}

void UFlecsInventoryWidget::RequestMoveItem(int64 ItemEntityId, FIntPoint NewGridPosition)
{
	if (!ContainerModel || ItemEntityId == 0) return;

	// Optimistic: model updates instantly, then sends to sim thread
	ContainerModel->MoveItem(ItemEntityId, NewGridPosition);
}

bool UFlecsInventoryWidget::CanFitAt(int64 ItemEntityId, FIntPoint Position) const
{
	if (!ContainerModel) return false;
	return ContainerModel->CanFitAt(ItemEntityId, Position);
}

bool UFlecsInventoryWidget::CanStackAt(int64 ItemEntityId, FIntPoint Position) const
{
	if (!ContainerModel) return false;
	return ContainerModel->CanStackAt(ItemEntityId, Position);
}

int32 UFlecsInventoryWidget::GetGridWidth() const
{
	return ContainerModel ? ContainerModel->GetGridWidth() : 0;
}

int32 UFlecsInventoryWidget::GetGridHeight() const
{
	return ContainerModel ? ContainerModel->GetGridHeight() : 0;
}

// ═══════════════════════════════════════════════════════════════
// IFlecsContainerView
// ═══════════════════════════════════════════════════════════════

void UFlecsInventoryWidget::OnContainerSnapshotReceived(const FContainerSnapshot& Snapshot)
{
	if (!bIsOpen || Snapshot.ContainerEntityId != ContainerEntityId) return;

	// Rebuild grid if dimensions changed
	if (SlotWidgets.Num() != Snapshot.GridWidth * Snapshot.GridHeight)
	{
		BuildGrid(Snapshot.GridWidth, Snapshot.GridHeight);
	}

	PopulateItems(Snapshot.Items);
}

void UFlecsInventoryWidget::OnItemMoved(int64 ItemEntityId, FIntPoint OldPos, FIntPoint NewPos)
{
	// Optimistic move already handled by model — just repopulate from model's Items
	if (!bIsOpen || !ContainerModel) return;
	PopulateItems(ContainerModel->GetItems());
}

void UFlecsInventoryWidget::OnOperationRolledBack(uint32 OpId)
{
	// Sim rejected the move — model already reconciled, repopulate
	if (!bIsOpen || !ContainerModel) return;
	PopulateItems(ContainerModel->GetItems());
}

// ═══════════════════════════════════════════════════════════════
// GRID BUILDING (virtual — override for custom visuals)
// ═══════════════════════════════════════════════════════════════

void UFlecsInventoryWidget::BuildGrid(int32 InGridWidth, int32 InGridHeight)
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

void UFlecsInventoryWidget::PopulateItems(const TArray<FContainerItemSnapshot>& Items)
{
	// Clear old item widgets
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
		ItemWidget->SetParentInventory(this);

		UCanvasPanelSlot* CanvasSlot = GridCanvas->AddChildToCanvas(ItemWidget);
		CanvasSlot->SetPosition(FVector2D(Item.GridPosition.X * CellSize, Item.GridPosition.Y * CellSize));
		CanvasSlot->SetSize(FVector2D(Item.GridSize.X * CellSize, Item.GridSize.Y * CellSize));
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetZOrder(1); // Above slots

		ItemWidgets.Add(ItemWidget);
	}
}

void UFlecsInventoryWidget::ClearAll()
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
