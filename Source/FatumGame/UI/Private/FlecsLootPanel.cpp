// UFlecsLootPanel: dual-grid loot panel with cross-container drag-drop.

#include "FlecsLootPanel.h"
#include "FlecsCharacter.h"
#include "FlecsContainerGridWidget.h"
#include "FlecsContainerModel.h"
#include "FlecsContainerLibrary.h"
#include "FlecsInventorySlotWidget.h"
#include "FlecsInventoryItemWidget.h"
#include "FlecsUISubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

// ═══════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════

void UFlecsLootPanel::BuildDefaultWidgetTree()
{
	// Root border
	UBorder* RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RootBorder"));
	RootBorder->SetBrushColor(FLinearColor(0.01f, 0.01f, 0.02f, 0.95f));
	RootBorder->SetPadding(FMargin(12.f));

	// Main vertical box (close button row + content row)
	UVerticalBox* MainVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MainVBox"));
	RootBorder->SetContent(MainVBox);

	// Close button row
	UHorizontalBox* CloseRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CloseRow"));
	{
		USpacer* CloseSpacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("CloseSpacer"));
		CloseSpacer->SetSize(FVector2D(1.f, 0.f));
		auto* CloseSpacerSlot = CloseRow->AddChildToHorizontalBox(CloseSpacer);
		CloseSpacerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseButton"));
		CloseButton->SetBackgroundColor(FLinearColor(0.5f, 0.1f, 0.1f, 0.8f));
		UTextBlock* CloseText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CloseText"));
		CloseText->SetText(FText::FromString(TEXT("X")));
		CloseText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		CloseButton->AddChild(CloseText);
		CloseRow->AddChildToHorizontalBox(CloseButton);
	}
	auto* CloseRowSlot = MainVBox->AddChildToVerticalBox(CloseRow);
	CloseRowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));

	// Content: HBox with two columns
	UHorizontalBox* HBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ContentHBox"));

	// Player side — grid widget created in PostInitialize (UUserWidget needs CreateWidget)
	PlayerVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PlayerVBox"));
	{
		PlayerTitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PlayerTitleText"));
		PlayerTitleText->SetText(FText::FromString(TEXT("Inventory")));
		PlayerTitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		auto* TitleSlot = PlayerVBox->AddChildToVerticalBox(PlayerTitleText);
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}
	HBox->AddChildToHorizontalBox(PlayerVBox);

	// Spacer between grids
	USpacer* GridSpacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("GridSpacer"));
	GridSpacer->SetSize(FVector2D(16.f, 0.f));
	HBox->AddChildToHorizontalBox(GridSpacer);

	// External side — grid widget created in PostInitialize (UUserWidget needs CreateWidget)
	ExternalVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ExternalVBox"));
	{
		ExternalTitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ExternalTitleText"));
		ExternalTitleText->SetText(FText::FromString(TEXT("Container")));
		ExternalTitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		auto* TitleSlot = ExternalVBox->AddChildToVerticalBox(ExternalTitleText);
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}
	HBox->AddChildToHorizontalBox(ExternalVBox);

	auto* ContentSlot = MainVBox->AddChildToVerticalBox(HBox);
	ContentSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	WidgetTree->RootWidget = RootBorder;
}

void UFlecsLootPanel::PostInitialize()
{
	// Create grid widgets via CreateWidget (UUserWidget subclasses can't use ConstructWidget)
	APlayerController* PC = GetOwningPlayer();
	check(PC);

	if (PlayerVBox)
	{
		PlayerGrid = CreateWidget<UFlecsContainerGridWidget>(PC);
		PlayerGrid->CellSize = CellSize;
		if (SlotWidgetClass) PlayerGrid->SlotWidgetClass = SlotWidgetClass;
		if (ItemWidgetClass) PlayerGrid->ItemWidgetClass = ItemWidgetClass;
		PlayerVBox->AddChildToVerticalBox(PlayerGrid);
	}
	if (ExternalVBox)
	{
		ExternalGrid = CreateWidget<UFlecsContainerGridWidget>(PC);
		ExternalGrid->CellSize = CellSize;
		if (SlotWidgetClass) ExternalGrid->SlotWidgetClass = SlotWidgetClass;
		if (ItemWidgetClass) ExternalGrid->ItemWidgetClass = ItemWidgetClass;
		ExternalVBox->AddChildToVerticalBox(ExternalGrid);
	}
	if (CloseButton)
	{
		CloseButton->OnClicked.AddDynamic(this, &UFlecsLootPanel::OnCloseButtonClicked);
	}
}

// ═══════════════════════════════════════════════════════════════
// PUBLIC API
// ═══════════════════════════════════════════════════════════════

void UFlecsLootPanel::SetOwningCharacter(AFlecsCharacter* InCharacter)
{
	OwningCharacter = InCharacter;
}

void UFlecsLootPanel::OpenLoot(int64 InPlayerContainerId, int64 InExternalContainerId, const FText& ExternalTitle)
{
	if (InPlayerContainerId == 0 || InExternalContainerId == 0) return;
	if (!PlayerGrid || !ExternalGrid) return;

	PlayerContainerEntityId = InPlayerContainerId;
	ExternalContainerEntityId = InExternalContainerId;
	bIsOpen = true;

	// Set title
	if (ExternalTitleText)
	{
		ExternalTitleText->SetText(ExternalTitle.IsEmpty() ? FText::FromString(TEXT("Container")) : ExternalTitle);
	}

	// Acquire models
	UFlecsUISubsystem* UISub = UFlecsUISubsystem::SelfPtr;
	check(UISub);
	PlayerModel = UISub->AcquireContainerModel(PlayerContainerEntityId);
	ExternalModel = UISub->AcquireContainerModel(ExternalContainerEntityId);

	// Bind grids
	PlayerGrid->BindContainer(PlayerModel, PlayerContainerEntityId);
	ExternalGrid->BindContainer(ExternalModel, ExternalContainerEntityId);

	// Wire cross-container drop callbacks
	PlayerGrid->OnCrossContainerDrop = [this](int64 SrcId, int64 ItemId, FIntPoint DestPos)
	{
		HandleCrossContainerDrop(SrcId, ItemId, DestPos, PlayerContainerEntityId);
	};
	ExternalGrid->OnCrossContainerDrop = [this](int64 SrcId, int64 ItemId, FIntPoint DestPos)
	{
		HandleCrossContainerDrop(SrcId, ItemId, DestPos, ExternalContainerEntityId);
	};

	ActivateWidget();

	UE_LOG(LogTemp, Log, TEXT("OpenLoot: Player=%lld External=%lld"), PlayerContainerEntityId, ExternalContainerEntityId);
}

void UFlecsLootPanel::CloseLoot()
{
	if (!bIsOpen) return;

	DeactivateWidget();

	bIsOpen = false;

	// Clear callbacks before unbinding
	if (PlayerGrid) PlayerGrid->OnCrossContainerDrop = nullptr;
	if (ExternalGrid) ExternalGrid->OnCrossContainerDrop = nullptr;

	if (PlayerGrid) PlayerGrid->UnbindContainer();
	if (ExternalGrid) ExternalGrid->UnbindContainer();

	UFlecsUISubsystem* UISub = UFlecsUISubsystem::SelfPtr;
	if (UISub)
	{
		if (PlayerModel) UISub->ReleaseModel(PlayerContainerEntityId);
		if (ExternalModel) UISub->ReleaseModel(ExternalContainerEntityId);
	}
	PlayerModel = nullptr;
	ExternalModel = nullptr;
	PlayerContainerEntityId = 0;
	ExternalContainerEntityId = 0;
}

// ═══════════════════════════════════════════════════════════════
// CROSS-CONTAINER TRANSFER
// ═══════════════════════════════════════════════════════════════

void UFlecsLootPanel::HandleCrossContainerDrop(int64 SrcContainerId, int64 ItemEntityId, FIntPoint DestPos, int64 DestContainerId)
{
	AFlecsCharacter* Character = OwningCharacter.Get();
	if (!Character) return;

	UFlecsContainerLibrary::TransferItem(Character, SrcContainerId, DestContainerId, ItemEntityId, DestPos);
}

void UFlecsLootPanel::OnCloseButtonClicked()
{
	if (AFlecsCharacter* Character = OwningCharacter.Get())
	{
		Character->CloseLootPanel();
	}
}
