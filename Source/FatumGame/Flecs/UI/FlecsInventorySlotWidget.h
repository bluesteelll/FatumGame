// Single grid cell in the inventory. Handles drag-drop target + visual highlight.
// Fully C++ — no Blueprint setup needed.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlecsInventorySlotWidget.generated.h"

class UBorder;
class UFlecsInventoryWidget;

UCLASS()
class FATUMGAME_API UFlecsInventorySlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitSlot(int32 InGridX, int32 InGridY, UFlecsInventoryWidget* InInventoryWidget);

	int32 GridX = 0;
	int32 GridY = 0;

protected:
	virtual bool Initialize() override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;
	virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;

private:
	void SetDragHighlight(bool bCanPlace);
	void ClearDragHighlight();

	UPROPERTY()
	TObjectPtr<UFlecsInventoryWidget> ParentInventory;

	UPROPERTY()
	TObjectPtr<UBorder> SlotBorder;
};
