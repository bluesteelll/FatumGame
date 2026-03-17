// Inventory item widget: shows item visuals, supports drag.
// Inherits UFlecsUIWidget for automatic C++/BP Designer dual-mode.
//
// Two usage modes:
//   1. Pure C++ (default): BuildDefaultWidgetTree() auto-creates Border + TextBlocks.
//   2. Blueprint Designer: Create WBP_Item child, add widgets in Designer.
//      BindWidgetOptional: "ItemBorder", "TextItemName", "TextCount".
//      Add images, materials, animations — full visual control.
//
// Override OnUpdateVisuals() in BP for custom rendering (icons, rarity borders, etc.).

#pragma once

#include "CoreMinimal.h"
#include "FlecsUIWidget.h"
#include "FlecsContainerTypes.h"
#include "FlecsInventoryItemWidget.generated.h"

class UBorder;
class UTextBlock;
class UFlecsContainerGridWidget;

UCLASS()
class FATUMGAME_API UFlecsInventoryItemWidget : public UFlecsUIWidget
{
	GENERATED_BODY()

public:
	/** Set all item data and update visuals. Call after CreateWidget. */
	void SetItemData(const FContainerItemSnapshot& Data);

	/** Must be set after creation so drops can be forwarded to the grid. */
	void SetParentGrid(UFlecsContainerGridWidget* InParent) { ParentGrid = InParent; }

	// ═══════════════════════════════════════════════════════════════
	// ITEM DATA (readable from BP)
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Item")
	int64 ItemEntityId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Item")
	FIntPoint GridPosition;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Item")
	FIntPoint GridSize = FIntPoint(1, 1);

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Item")
	FName TypeId;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Item")
	int32 Count = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Item")
	int32 MaxStack = 99;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Item")
	int32 RarityTier = 0;

	/** ItemDefinition DA — use to read icon, description, etc. in BP. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Item")
	TObjectPtr<UDataAsset> ItemDefinition;

	/** Execute the item's default action (right-click). Currently handles Consume for vitals. */
	void ExecuteDefaultAction();

protected:
	virtual void BuildDefaultWidgetTree() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;

	// Forward drag-drop events to the slot underneath (item widgets sit on top at z=1)
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

	// ═══════════════════════════════════════════════════════════════
	// VIRTUAL — override in BP for custom rendering
	// Base class provides OnUpdateVisuals (BlueprintNativeEvent).
	// Called via RefreshVisuals() after SetItemData().
	// All item data fields (TypeId, Count, RarityTier, ItemDefinition) are already set.
	// ═══════════════════════════════════════════════════════════════

	virtual void OnUpdateVisuals_Implementation() override;

	// ═══════════════════════════════════════════════════════════════
	// DESIGNER-BINDABLE + VISUAL CONFIG
	// ═══════════════════════════════════════════════════════════════

	/** Root border. Name in Designer: "ItemBorder". */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> ItemBorder;

	/** Item name text. Name in Designer: "TextItemName". */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TextItemName;

	/** Stack count text. Name in Designer: "TextCount". */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TextCount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Item|Colors")
	FLinearColor ItemBackgroundColor = FLinearColor(0.15f, 0.15f, 0.2f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Item|Colors")
	FLinearColor ItemNameColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Item|Colors")
	FLinearColor CountTextColor = FLinearColor(0.7f, 0.7f, 0.7f);

private:
	UPROPERTY()
	TObjectPtr<UFlecsContainerGridWidget> ParentGrid;
};
