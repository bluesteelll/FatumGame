// UFlecsInventoryWidget: thin wrapper — delegates grid to UFlecsContainerGridWidget.

#include "FlecsInventoryWidget.h"
#include "FlecsCharacter.h"
#include "FlecsContainerModel.h"
#include "FlecsContainerGridWidget.h"
#include "FlecsInventorySlotWidget.h"
#include "FlecsInventoryItemWidget.h"
#include "FlecsUISubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"

// ═══════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════

void UFlecsInventoryWidget::BuildDefaultWidgetTree()
{
	// Simple canvas as root — grid widget added as child in PostInitialize
	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;
}

void UFlecsInventoryWidget::PostInitialize()
{
	// Create grid widget via CreateWidget (proper UUserWidget initialization)
	ContainerGrid = CreateWidget<UFlecsContainerGridWidget>(GetOwningPlayer(), UFlecsContainerGridWidget::StaticClass());
	check(ContainerGrid);

	ContainerGrid->CellSize = CellSize;
	if (SlotWidgetClass) ContainerGrid->SlotWidgetClass = SlotWidgetClass;
	if (ItemWidgetClass) ContainerGrid->ItemWidgetClass = ItemWidgetClass;

	// Add as child of root canvas
	if (UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget))
	{
		UCanvasPanelSlot* GridSlot = RootCanvas->AddChildToCanvas(ContainerGrid);
		GridSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		GridSlot->SetOffsets(FMargin(0.f));
	}
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
	if (InContainerEntityId == 0 || !ContainerGrid) return;

	ContainerEntityId = InContainerEntityId;
	bIsOpen = true;

	UFlecsUISubsystem* UISub = UFlecsUISubsystem::SelfPtr;
	check(UISub);
	ContainerModel = UISub->AcquireContainerModel(ContainerEntityId);
	ContainerGrid->BindContainer(ContainerModel, ContainerEntityId);

	ActivateWidget();

	UE_LOG(LogTemp, Log, TEXT("OpenInventory: ContainerId=%lld Model=%s Grid=%dx%d"),
		ContainerEntityId, *ContainerModel->GetName(),
		ContainerModel->GetGridWidth(), ContainerModel->GetGridHeight());
}

void UFlecsInventoryWidget::CloseInventory()
{
	if (!bIsOpen) return;

	DeactivateWidget();

	bIsOpen = false;

	if (ContainerGrid)
	{
		ContainerGrid->UnbindContainer();
	}

	if (ContainerModel)
	{
		if (UFlecsUISubsystem* UISub = UFlecsUISubsystem::SelfPtr)
		{
			UISub->ReleaseModel(ContainerEntityId);
		}
		ContainerModel = nullptr;
	}

	ContainerEntityId = 0;
}
