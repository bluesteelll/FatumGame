// Inventory item widget: shows item name + count, supports drag.
// Fully C++ — no Blueprint setup needed.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlecsContainerTypes.h"
#include "FlecsInventoryItemWidget.generated.h"

class UBorder;
class UTextBlock;
class UFlecsInventoryWidget;

UCLASS()
class FATUMGAME_API UFlecsInventoryItemWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Set all item data and update visuals. Call after CreateWidget. */
	void SetItemData(const FContainerItemSnapshot& Data);

	/** Must be set after creation so drops can be forwarded to the inventory. */
	void SetParentInventory(UFlecsInventoryWidget* InParent) { ParentInventory = InParent; }

	int64 ItemEntityId = 0;
	FIntPoint GridPosition;
	FIntPoint GridSize = FIntPoint(1, 1);
	FName TypeId;
	int32 Count = 1;

protected:
	virtual bool Initialize() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;

	// Forward drag-drop events to the slot underneath (item widgets sit on top at z=1)
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

private:
	UPROPERTY()
	TObjectPtr<UBorder> ItemBorder;

	UPROPERTY()
	TObjectPtr<UTextBlock> TextItemName;

	UPROPERTY()
	TObjectPtr<UTextBlock> TextCount;

	UPROPERTY()
	TObjectPtr<UFlecsInventoryWidget> ParentInventory;
};
