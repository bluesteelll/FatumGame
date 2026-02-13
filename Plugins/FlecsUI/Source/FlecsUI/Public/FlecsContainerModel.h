// Container ViewModel for FlecsUI.
// Manages item snapshots, occupancy grid, and optimistic updates with rollback.
// Game thread only — snapshot data pushed by subsystem from triple buffer.

#pragma once

#include "CoreMinimal.h"
#include "FlecsUIModel.h"
#include "FlecsContainerTypes.h"
#include "FlecsContainerModel.generated.h"

class IFlecsContainerView;

UCLASS()
class FLECSUI_API UFlecsContainerModel : public UFlecsUIModel
{
	GENERATED_BODY()

public:
	// ═══ Queries (instant, local cache — game thread only) ═══

	int32 GetGridWidth() const { return GridWidth; }
	int32 GetGridHeight() const { return GridHeight; }
	float GetMaxWeight() const { return MaxWeight; }
	float GetCurrentWeight() const { return CurrentWeight; }
	const TArray<FContainerItemSnapshot>& GetItems() const { return Items; }

	/** Check if an item can fit at the given position (free space OR stackable target). */
	bool CanFitAt(int64 ItemEntityId, FIntPoint Position) const;

	/** Check if item can stack at position (same type, not full). */
	bool CanStackAt(int64 ItemEntityId, FIntPoint Position) const;

	/** Find item snapshot by entity ID. Returns nullptr if not found. */
	const FContainerItemSnapshot* FindItem(int64 ItemEntityId) const;

	/** Check if a given size can fit at position (bounds + occupancy, no item lookup).
	 *  Used for cross-container drag highlight where the item is not in this model. */
	bool CanFitSizeAt(FIntPoint Position, FIntPoint Size) const;

	// ═══ Commands (optimistic + async confirm) ═══

	/** Move an item. Returns handle for tracking. Visual updates INSTANTLY. */
	FUIOpHandle MoveItem(int64 ItemEntityId, FIntPoint NewPos, FOnUIOpComplete OnComplete = {});

	// ═══ Sim-thread command callbacks (set by subsystem during bridge setup) ═══

	/** Called by MoveItem to execute the actual sim-thread mutation. */
	TFunction<void(uint32 OpId, int64 ItemEntityId, FIntPoint NewPos)> MoveItemOnSim;

	// ═══ Snapshot Receive (called by subsystem after triple buffer read) ═══

	void ReceiveSnapshot(const FContainerSnapshot& Snapshot);

	// ═══ Lifecycle ═══

	virtual void Activate(flecs::entity InEntity) override;
	virtual void Deactivate() override;

private:
	/** Apply optimistic position change (game thread only). */
	void ApplyOptimisticMove(int64 ItemEntityId, FIntPoint OldPos, FIntPoint NewPos);

	/** Rebuild occupancy mask from Items array. */
	void RebuildOccupancy();

	/** Reconcile local state with authoritative snapshot from sim. */
	void ReconcileWithSnapshot(const FContainerSnapshot& Snapshot);

	/** Notify all bound IFlecsContainerView instances of a new snapshot. */
	void NotifyViewsSnapshot(const FContainerSnapshot& Snapshot);

	/** Notify all bound IFlecsContainerView instances of an item move. */
	void NotifyViewsItemMoved(int64 ItemEntityId, FIntPoint OldPos, FIntPoint NewPos);

	/** Notify all bound IFlecsContainerView instances of a rollback. */
	void NotifyViewsRollback(uint32 OpId);

	// ═══ State ═══

	TArray<FContainerItemSnapshot> Items;
	TArray<uint8> OccupancyMask;
	int32 GridWidth = 0;
	int32 GridHeight = 0;
	float MaxWeight = -1.f;
	float CurrentWeight = 0.f;
};
