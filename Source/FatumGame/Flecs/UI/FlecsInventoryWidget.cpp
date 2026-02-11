// UFlecsInventoryWidget: Grid inventory, fully built in C++.

#include "FlecsInventoryWidget.h"
#include "FlecsCharacter.h"
#include "FlecsContainerLibrary.h"
#include "FlecsInventorySlotWidget.h"
#include "FlecsInventoryItemWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"

// ═══════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════

bool UFlecsInventoryWidget::Initialize()
{
	if (!Super::Initialize()) return false;

	// Build widget tree only if no Blueprint Designer content exists
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BackgroundBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Background"));
		BackgroundBorder->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.05f, 0.92f));
		BackgroundBorder->SetPadding(FMargin(8.f));

		GridCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("GridCanvas"));
		BackgroundBorder->SetContent(GridCanvas);

		WidgetTree->RootWidget = BackgroundBorder;
	}

	return true;
}

void UFlecsInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UFlecsMessageSubsystem* MsgSub = UFlecsMessageSubsystem::Get(this);
	check(MsgSub);

	SnapshotHandle = MsgSub->RegisterListener<FUIInventorySnapshotMessage>(
		TAG_UI_InventorySnapshot, this,
		[this](FGameplayTag Tag, const FUIInventorySnapshotMessage& Msg) { HandleSnapshot(Tag, Msg); });

	ChangedHandle = MsgSub->RegisterListener<FUIInventoryChangedMessage>(
		TAG_UI_InventoryChanged, this,
		[this](FGameplayTag Tag, const FUIInventoryChangedMessage& Msg) { HandleChanged(Tag, Msg); });
}

void UFlecsInventoryWidget::NativeDestruct()
{
	SnapshotHandle.Unregister();
	ChangedHandle.Unregister();
	Super::NativeDestruct();
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

	int32 GW = 0, GH = 0;
	float MW = 0.f, CW = 0.f;
	int32 CC = 0;
	bool bFound = UFlecsContainerLibrary::GetContainerInfo(this, ContainerEntityId, GW, GH, MW, CW, CC);
	CachedGridWidth = GW;
	CachedGridHeight = GH;

	UE_LOG(LogTemp, Log, TEXT("OpenInventory: ContainerId=%lld Found=%d Grid=%dx%d Items=%d"),
		ContainerEntityId, bFound, GW, GH, CC);

	BuildGrid(CachedGridWidth, CachedGridHeight);
	RequestSnapshot();
}

void UFlecsInventoryWidget::CloseInventory()
{
	if (!bIsOpen) return;

	bIsOpen = false;
	ContainerEntityId = 0;
	CachedItems.Empty();
	LocalOccupancyMask.Empty();
	ClearAll();
}

void UFlecsInventoryWidget::RequestMoveItem(int64 ItemEntityId, FIntPoint NewGridPosition)
{
	if (ContainerEntityId == 0 || ItemEntityId == 0) return;
	UFlecsContainerLibrary::MoveItemInContainer(this, ContainerEntityId, ItemEntityId, NewGridPosition);
}

bool UFlecsInventoryWidget::CanFitAt(int64 ItemEntityId, FIntPoint Position) const
{
	FIntPoint ItemSize(1, 1);
	FIntPoint CurrentPos(-1, -1);
	for (const FInventoryItemSnapshot& Item : CachedItems)
	{
		if (Item.ItemEntityId == ItemEntityId)
		{
			ItemSize = Item.GridSize;
			CurrentPos = Item.GridPosition;
			break;
		}
	}

	const bool bCanFit = LocalCanFit(Position, ItemSize, ItemEntityId);
	const bool bCanStack = !bCanFit ? CanStackAt(ItemEntityId, Position) : false;

	UE_LOG(LogTemp, Verbose, TEXT("CanFitAt: Item=%lld Pos=(%d,%d) CurPos=(%d,%d) Size=(%d,%d) CanFit=%d CanStack=%d CachedItems=%d OccMask=%d"),
		ItemEntityId, Position.X, Position.Y, CurrentPos.X, CurrentPos.Y,
		ItemSize.X, ItemSize.Y, bCanFit, bCanStack, CachedItems.Num(), LocalOccupancyMask.Num());

	return bCanFit || bCanStack;
}

bool UFlecsInventoryWidget::CanStackAt(int64 ItemEntityId, FIntPoint Position) const
{
	const FInventoryItemSnapshot* SourceItem = nullptr;
	for (const FInventoryItemSnapshot& Item : CachedItems)
	{
		if (Item.ItemEntityId == ItemEntityId)
		{
			SourceItem = &Item;
			break;
		}
	}
	if (!SourceItem || SourceItem->MaxStack <= 1) return false;

	const FInventoryItemSnapshot* TargetItem = FindItemAtCell(Position, ItemEntityId);
	if (!TargetItem) return false;

	return TargetItem->ItemName == SourceItem->ItemName
		&& TargetItem->MaxStack > 1
		&& TargetItem->Count < TargetItem->MaxStack;
}

// ═══════════════════════════════════════════════════════════════
// GRID BUILDING (virtual — override for custom visuals)
// ═══════════════════════════════════════════════════════════════

void UFlecsInventoryWidget::BuildGrid(int32 GridWidth, int32 GridHeight)
{
	ClearAll();
	if (!GridCanvas) return;

	for (int32 Y = 0; Y < GridHeight; ++Y)
	{
		for (int32 X = 0; X < GridWidth; ++X)
		{
			UFlecsInventorySlotWidget* SlotWidget = CreateWidget<UFlecsInventorySlotWidget>(GetOwningPlayer());
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

void UFlecsInventoryWidget::PopulateItems(const TArray<FInventoryItemSnapshot>& Items)
{
	// Clear old item widgets
	for (UFlecsInventoryItemWidget* W : ItemWidgets)
	{
		if (W) W->RemoveFromParent();
	}
	ItemWidgets.Empty();

	if (!GridCanvas) return;

	for (const FInventoryItemSnapshot& Item : Items)
	{
		if (Item.GridPosition.X < 0) continue;

		UFlecsInventoryItemWidget* ItemWidget = CreateWidget<UFlecsInventoryItemWidget>(GetOwningPlayer());
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

// ═══════════════════════════════════════════════════════════════
// MESSAGE HANDLERS
// ═══════════════════════════════════════════════════════════════

void UFlecsInventoryWidget::HandleSnapshot(FGameplayTag Channel, const FUIInventorySnapshotMessage& Msg)
{
	if (Msg.ContainerEntityId != ContainerEntityId || !bIsOpen) return;

	CachedGridWidth = Msg.GridWidth;
	CachedGridHeight = Msg.GridHeight;
	CachedItems = Msg.Items;
	RebuildLocalOccupancy();
	PopulateItems(CachedItems);
}

void UFlecsInventoryWidget::HandleChanged(FGameplayTag Channel, const FUIInventoryChangedMessage& Msg)
{
	if (Msg.ContainerEntityId != ContainerEntityId || !bIsOpen) return;
	RequestSnapshot();
}

void UFlecsInventoryWidget::RequestSnapshot()
{
	if (ContainerEntityId == 0) return;
	UFlecsContainerLibrary::RequestContainerSnapshot(this, ContainerEntityId);
}

// ═══════════════════════════════════════════════════════════════
// LOCAL OCCUPANCY MIRROR
// ═══════════════════════════════════════════════════════════════

void UFlecsInventoryWidget::RebuildLocalOccupancy()
{
	const int32 TotalCells = CachedGridWidth * CachedGridHeight;
	if (TotalCells <= 0)
	{
		LocalOccupancyMask.Empty();
		return;
	}

	const int32 BytesNeeded = (TotalCells + 7) / 8;
	LocalOccupancyMask.SetNumUninitialized(BytesNeeded);
	FMemory::Memzero(LocalOccupancyMask.GetData(), BytesNeeded);

	for (const FInventoryItemSnapshot& Item : CachedItems)
	{
		if (Item.GridPosition.X < 0) continue;

		for (int32 Y = Item.GridPosition.Y; Y < Item.GridPosition.Y + Item.GridSize.Y; ++Y)
		{
			for (int32 X = Item.GridPosition.X; X < Item.GridPosition.X + Item.GridSize.X; ++X)
			{
				const int32 Idx = Y * CachedGridWidth + X;
				LocalOccupancyMask[Idx / 8] |= (1 << (Idx % 8));
			}
		}
	}
}

bool UFlecsInventoryWidget::LocalCanFit(FIntPoint Position, FIntPoint Size, int64 ExcludeItemEntityId) const
{
	if (Position.X < 0 || Position.Y < 0) return false;
	if (Position.X + Size.X > CachedGridWidth || Position.Y + Size.Y > CachedGridHeight) return false;
	if (LocalOccupancyMask.Num() == 0) return false;

	TSet<int32> ExcludedCells;
	if (ExcludeItemEntityId != 0)
	{
		for (const FInventoryItemSnapshot& Item : CachedItems)
		{
			if (Item.ItemEntityId == ExcludeItemEntityId && Item.GridPosition.X >= 0)
			{
				for (int32 Y = Item.GridPosition.Y; Y < Item.GridPosition.Y + Item.GridSize.Y; ++Y)
				{
					for (int32 X = Item.GridPosition.X; X < Item.GridPosition.X + Item.GridSize.X; ++X)
					{
						ExcludedCells.Add(Y * CachedGridWidth + X);
					}
				}
				break;
			}
		}
	}

	for (int32 Y = Position.Y; Y < Position.Y + Size.Y; ++Y)
	{
		for (int32 X = Position.X; X < Position.X + Size.X; ++X)
		{
			const int32 Idx = Y * CachedGridWidth + X;
			if (ExcludedCells.Contains(Idx)) continue;
			if (LocalOccupancyMask[Idx / 8] & (1 << (Idx % 8)))
			{
				return false;
			}
		}
	}
	return true;
}

const FInventoryItemSnapshot* UFlecsInventoryWidget::FindItemAtCell(FIntPoint Cell, int64 ExcludeItemEntityId) const
{
	for (const FInventoryItemSnapshot& Item : CachedItems)
	{
		if (Item.ItemEntityId == ExcludeItemEntityId || Item.GridPosition.X < 0) continue;

		if (Cell.X >= Item.GridPosition.X && Cell.X < Item.GridPosition.X + Item.GridSize.X
			&& Cell.Y >= Item.GridPosition.Y && Cell.Y < Item.GridPosition.Y + Item.GridSize.Y)
		{
			return &Item;
		}
	}
	return nullptr;
}
