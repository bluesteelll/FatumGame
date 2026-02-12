// View interface for container/inventory UI.
// Widgets implement this to receive container model updates.

#pragma once

#include "CoreMinimal.h"
#include "IFlecsUIView.h"
#include "FlecsContainerTypes.h"
#include "IFlecsContainerView.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UFlecsContainerView : public UFlecsUIView
{
	GENERATED_BODY()
};

class FLECSUI_API IFlecsContainerView : public IFlecsUIView
{
	GENERATED_BODY()

public:
	/** Called when a new container snapshot is received from the sim thread. */
	virtual void OnContainerSnapshotReceived(const FContainerSnapshot& Snapshot) {}

	/** Called when an item was moved optimistically (instant visual feedback). */
	virtual void OnItemMoved(int64 ItemEntityId, FIntPoint OldPosition, FIntPoint NewPosition) {}

	/** Called when an optimistic operation was rolled back by the sim thread. */
	virtual void OnOperationRolledBack(uint32 OpId) {}
};
