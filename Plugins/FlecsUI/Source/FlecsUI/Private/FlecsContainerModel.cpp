// UFlecsContainerModel — container ViewModel implementation.

#include "FlecsContainerModel.h"
#include "IFlecsContainerView.h"

void UFlecsContainerModel::Activate(flecs::entity InEntity)
{
	Super::Activate(InEntity);
	Items.Empty();
	OccupancyMask.Empty();
	GridWidth = 0;
	GridHeight = 0;
	MaxWeight = -1.f;
	CurrentWeight = 0.f;
}

void UFlecsContainerModel::Deactivate()
{
	Items.Empty();
	OccupancyMask.Empty();
	GridWidth = 0;
	GridHeight = 0;
	Super::Deactivate();
}

bool UFlecsContainerModel::CanFitAt(int64 ItemEntityId, FIntPoint Position) const
{
	const FContainerItemSnapshot* Item = FindItem(ItemEntityId);
	if (!Item) return false;

	const FIntPoint Size = Item->GridSize;

	// Bounds check
	if (Position.X < 0 || Position.Y < 0 ||
		Position.X + Size.X > GridWidth || Position.Y + Size.Y > GridHeight)
	{
		return false;
	}

	// Check occupancy (skip cells occupied by the item itself)
	for (int32 dy = 0; dy < Size.Y; ++dy)
	{
		for (int32 dx = 0; dx < Size.X; ++dx)
		{
			const int32 Idx = (Position.Y + dy) * GridWidth + (Position.X + dx);
			if (OccupancyMask.IsValidIndex(Idx) && OccupancyMask[Idx] != 0)
			{
				// Check if this cell is occupied by the item itself (allowed — it's moving)
				bool bOccupiedBySelf = false;
				const FIntPoint OldPos = Item->GridPosition;
				if (Position.X + dx >= OldPos.X && Position.X + dx < OldPos.X + Size.X &&
					Position.Y + dy >= OldPos.Y && Position.Y + dy < OldPos.Y + Size.Y)
				{
					bOccupiedBySelf = true;
				}

				if (!bOccupiedBySelf)
				{
					// Check for stackable target
					if (CanStackAt(ItemEntityId, Position))
					{
						return true;
					}
					return false;
				}
			}
		}
	}

	return true;
}

bool UFlecsContainerModel::CanStackAt(int64 ItemEntityId, FIntPoint Position) const
{
	const FContainerItemSnapshot* DraggedItem = FindItem(ItemEntityId);
	if (!DraggedItem) return false;

	// Find item at target position
	for (const auto& Other : Items)
	{
		if (Other.ItemEntityId == ItemEntityId) continue;

		if (Other.GridPosition == Position &&
			Other.TypeId == DraggedItem->TypeId &&
			Other.Count < Other.MaxStack)
		{
			return true;
		}
	}

	return false;
}

const FContainerItemSnapshot* UFlecsContainerModel::FindItem(int64 ItemEntityId) const
{
	for (const auto& Item : Items)
	{
		if (Item.ItemEntityId == ItemEntityId)
		{
			return &Item;
		}
	}
	return nullptr;
}

FUIOpHandle UFlecsContainerModel::MoveItem(int64 ItemEntityId, FIntPoint NewPos, FOnUIOpComplete OnComplete)
{
	FUIOpHandle Handle;
	Handle.OpId = AllocateOpId();

	// Find current position
	const FContainerItemSnapshot* Item = FindItem(ItemEntityId);
	if (!Item)
	{
		if (OnComplete.IsBound())
		{
			OnComplete.Execute(EUIOpResult::Failed);
		}
		return Handle;
	}

	const FIntPoint OldPos = Item->GridPosition;

	// Optimistic update — instant visual feedback
	ApplyOptimisticMove(ItemEntityId, OldPos, NewPos);
	NotifyViewsItemMoved(ItemEntityId, OldPos, NewPos);

	// Track pending op
	FPendingOp PendingOp;
	PendingOp.OpId = Handle.OpId;
	if (OnComplete.IsBound())
	{
		PendingOp.OnComplete = [OnComplete](EUIOpResult Result) { OnComplete.Execute(Result); };
	}
	PendingOps.Add(MoveTemp(PendingOp));

	// Send to sim thread
	if (MoveItemOnSim)
	{
		MoveItemOnSim(Handle.OpId, ItemEntityId, NewPos);
	}

	return Handle;
}

void UFlecsContainerModel::ReceiveSnapshot(const FContainerSnapshot& Snapshot)
{
	ReconcileWithSnapshot(Snapshot);
	NotifyViewsSnapshot(Snapshot);
}

void UFlecsContainerModel::ApplyOptimisticMove(int64 ItemEntityId, FIntPoint OldPos, FIntPoint NewPos)
{
	for (auto& Item : Items)
	{
		if (Item.ItemEntityId == ItemEntityId)
		{
			Item.GridPosition = NewPos;
			break;
		}
	}
	RebuildOccupancy();
}

void UFlecsContainerModel::RebuildOccupancy()
{
	const int32 TotalCells = GridWidth * GridHeight;
	if (TotalCells <= 0) return;

	OccupancyMask.SetNumUninitialized(TotalCells);
	FMemory::Memzero(OccupancyMask.GetData(), TotalCells);

	for (const auto& Item : Items)
	{
		const FIntPoint Pos = Item.GridPosition;
		const FIntPoint Size = Item.GridSize;

		for (int32 dy = 0; dy < Size.Y; ++dy)
		{
			for (int32 dx = 0; dx < Size.X; ++dx)
			{
				const int32 Idx = (Pos.Y + dy) * GridWidth + (Pos.X + dx);
				if (Idx >= 0 && Idx < TotalCells)
				{
					OccupancyMask[Idx] = 1;
				}
			}
		}
	}
}

void UFlecsContainerModel::ReconcileWithSnapshot(const FContainerSnapshot& Snapshot)
{
	// Apply authoritative snapshot as base
	GridWidth = Snapshot.GridWidth;
	GridHeight = Snapshot.GridHeight;
	MaxWeight = Snapshot.MaxWeight;
	CurrentWeight = Snapshot.CurrentWeight;
	Items = Snapshot.Items;

	// Re-apply unconfirmed pending ops on top of snapshot
	for (const auto& Op : PendingOps)
	{
		// For now, pending ops are just moves — the optimistic position is already
		// in the snapshot if confirmed, or needs re-application if not yet confirmed.
		// Since we use latest-value-wins triple buffer, the snapshot reflects the latest
		// sim state. Unconfirmed ops will be reconciled when their OpResult arrives.
	}

	RebuildOccupancy();
}

void UFlecsContainerModel::NotifyViewsSnapshot(const FContainerSnapshot& Snapshot)
{
	for (auto& Weak : Views)
	{
		if (auto* Obj = Weak.Get())
		{
			if (auto* View = Cast<IFlecsContainerView>(Obj))
			{
				View->OnContainerSnapshotReceived(Snapshot);
			}
		}
	}
}

void UFlecsContainerModel::NotifyViewsItemMoved(int64 ItemEntityId, FIntPoint OldPos, FIntPoint NewPos)
{
	for (auto& Weak : Views)
	{
		if (auto* Obj = Weak.Get())
		{
			if (auto* View = Cast<IFlecsContainerView>(Obj))
			{
				View->OnItemMoved(ItemEntityId, OldPos, NewPos);
			}
		}
	}
}

void UFlecsContainerModel::NotifyViewsRollback(uint32 OpId)
{
	for (auto& Weak : Views)
	{
		if (auto* Obj = Weak.Get())
		{
			if (auto* View = Cast<IFlecsContainerView>(Obj))
			{
				View->OnOperationRolledBack(OpId);
			}
		}
	}
}
