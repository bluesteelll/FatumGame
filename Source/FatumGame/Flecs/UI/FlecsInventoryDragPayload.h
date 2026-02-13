// Drag-and-drop operation payload for inventory items.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "FlecsInventoryDragPayload.generated.h"

UCLASS(BlueprintType)
class FATUMGAME_API UFlecsInventoryDragOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	/** Flecs entity ID of the item being dragged. */
	UPROPERTY(BlueprintReadWrite, Category = "Inventory")
	int64 ItemEntityId = 0;

	/** Original grid position before drag started. */
	UPROPERTY(BlueprintReadWrite, Category = "Inventory")
	FIntPoint OriginalPosition = FIntPoint(-1, -1);

	/** Item grid size (from FItemStaticData). */
	UPROPERTY(BlueprintReadWrite, Category = "Inventory")
	FIntPoint GridSize = FIntPoint(1, 1);

	/** Source container entity ID (for cross-container drag detection). */
	UPROPERTY(BlueprintReadWrite, Category = "Inventory")
	int64 SourceContainerEntityId = 0;
};
