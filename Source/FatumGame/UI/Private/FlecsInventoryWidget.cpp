// UFlecsInventoryWidget: inventory grid + weapon slot grid side by side.

#include "FlecsInventoryWidget.h"
#include "FlecsCharacter.h"
#include "FlecsContainerModel.h"
#include "FlecsContainerGridWidget.h"
#include "FlecsContainerLibrary.h"
#include "FlecsInventorySlotWidget.h"
#include "FlecsInventoryItemWidget.h"
#include "FlecsUISubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Spacer.h"

// ═══════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════

void UFlecsInventoryWidget::BuildDefaultWidgetTree()
{
	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;
}

void UFlecsInventoryWidget::PostInitialize()
{
	APlayerController* PC = GetOwningPlayer();

	// Main inventory grid
	ContainerGrid = CreateWidget<UFlecsContainerGridWidget>(PC, UFlecsContainerGridWidget::StaticClass());
	check(ContainerGrid);
	ContainerGrid->CellSize = CellSize;
	if (SlotWidgetClass) ContainerGrid->SlotWidgetClass = SlotWidgetClass;
	if (ItemWidgetClass) ContainerGrid->ItemWidgetClass = ItemWidgetClass;

	// Weapon slot grid (2x1)
	WeaponSlotGrid = CreateWidget<UFlecsContainerGridWidget>(PC, UFlecsContainerGridWidget::StaticClass());
	check(WeaponSlotGrid);
	WeaponSlotGrid->CellSize = CellSize;
	if (SlotWidgetClass) WeaponSlotGrid->SlotWidgetClass = SlotWidgetClass;
	if (ItemWidgetClass) WeaponSlotGrid->ItemWidgetClass = ItemWidgetClass;

	// Layout: HBox [ WeaponSlots | Spacer | InventoryGrid ]
	if (UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget))
	{
		UHorizontalBox* HBox = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), TEXT("MainHBox"));
		UCanvasPanelSlot* HBoxSlot = RootCanvas->AddChildToCanvas(HBox);
		HBoxSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		HBoxSlot->SetOffsets(FMargin(0.f));

		// Weapon slots column (label + grid)
		UVerticalBox* WeaponVBox = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("WeaponVBox"));

		UTextBlock* WeaponLabel = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("WeaponLabel"));
		WeaponLabel->SetText(FText::FromString(TEXT("Weapons")));
		UVerticalBoxSlot* LabelSlot = WeaponVBox->AddChildToVerticalBox(WeaponLabel);
		LabelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		UVerticalBoxSlot* WepGridSlot = WeaponVBox->AddChildToVerticalBox(WeaponSlotGrid);
		WepGridSlot->SetHorizontalAlignment(HAlign_Left);

		UHorizontalBoxSlot* WeaponColSlot = HBox->AddChildToHorizontalBox(WeaponVBox);
		WeaponColSlot->SetHorizontalAlignment(HAlign_Left);
		WeaponColSlot->SetVerticalAlignment(VAlign_Top);

		// Spacer
		USpacer* Sp = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("GridSpacer"));
		Sp->SetSize(FVector2D(16.f, 0.f));
		HBox->AddChildToHorizontalBox(Sp);

		// Inventory grid column
		UHorizontalBoxSlot* GridColSlot = HBox->AddChildToHorizontalBox(ContainerGrid);
		GridColSlot->SetHorizontalAlignment(HAlign_Fill);
		GridColSlot->SetVerticalAlignment(VAlign_Top);
		FSlateChildSize FillSize;
		FillSize.SizeRule = ESlateSizeRule::Fill;
		FillSize.Value = 1.f;
		GridColSlot->SetSize(FillSize);
	}
}

// ═══════════════════════════════════════════════════════════════
// PUBLIC API
// ═══════════════════════════════════════════════════════════════

void UFlecsInventoryWidget::SetOwningCharacter(AFlecsCharacter* InCharacter)
{
	OwningCharacter = InCharacter;
}

void UFlecsInventoryWidget::OpenInventory(int64 InContainerEntityId, int64 InWeaponContainerEntityId)
{
	if (InContainerEntityId == 0 || !ContainerGrid) return;

	ContainerEntityId = InContainerEntityId;
	WeaponContainerEntityId = InWeaponContainerEntityId;
	bIsOpen = true;

	UFlecsUISubsystem* UISub = UFlecsUISubsystem::SelfPtr;
	check(UISub);

	// Bind main inventory
	ContainerModel = UISub->AcquireContainerModel(ContainerEntityId);
	ContainerGrid->BindContainer(ContainerModel, ContainerEntityId);

	// Bind weapon slots (if available)
	if (WeaponContainerEntityId != 0 && WeaponSlotGrid)
	{
		WeaponSlotModel = UISub->AcquireContainerModel(WeaponContainerEntityId);
		WeaponSlotGrid->BindContainer(WeaponSlotModel, WeaponContainerEntityId);
		WeaponSlotGrid->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	else if (WeaponSlotGrid)
	{
		WeaponSlotGrid->SetVisibility(ESlateVisibility::Collapsed);
	}

	// Wire cross-container drops: inventory ↔ weapon slots
	ContainerGrid->OnCrossContainerDrop = [this](int64 SrcId, int64 ItemId, FIntPoint DestPos)
	{
		HandleCrossContainerDrop(SrcId, ItemId, DestPos, ContainerEntityId);
	};

	if (WeaponSlotGrid)
	{
		WeaponSlotGrid->OnCrossContainerDrop = [this](int64 SrcId, int64 ItemId, FIntPoint DestPos)
		{
			HandleCrossContainerDrop(SrcId, ItemId, DestPos, WeaponContainerEntityId);
		};
	}

	ActivateWidget();

	UE_LOG(LogTemp, Log, TEXT("OpenInventory: ContainerId=%lld WeaponContainerId=%lld"),
		ContainerEntityId, WeaponContainerEntityId);
}

void UFlecsInventoryWidget::CloseInventory()
{
	if (!bIsOpen) return;

	DeactivateWidget();
	bIsOpen = false;

	if (ContainerGrid)
		ContainerGrid->UnbindContainer();

	if (WeaponSlotGrid)
		WeaponSlotGrid->UnbindContainer();

	if (ContainerModel)
	{
		if (UFlecsUISubsystem* UISub = UFlecsUISubsystem::SelfPtr)
			UISub->ReleaseModel(ContainerEntityId);
		ContainerModel = nullptr;
	}

	if (WeaponSlotModel)
	{
		if (UFlecsUISubsystem* UISub = UFlecsUISubsystem::SelfPtr)
			UISub->ReleaseModel(WeaponContainerEntityId);
		WeaponSlotModel = nullptr;
	}

	ContainerEntityId = 0;
	WeaponContainerEntityId = 0;
}

void UFlecsInventoryWidget::HandleCrossContainerDrop(int64 SourceContainerId, int64 ItemEntityId, FIntPoint DestPos, int64 DestContainerId)
{
	AFlecsCharacter* Character = OwningCharacter.Get();
	if (!Character) return;

	UFlecsContainerLibrary::TransferItem(Character, SourceContainerId, DestContainerId, ItemEntityId, DestPos);
}
