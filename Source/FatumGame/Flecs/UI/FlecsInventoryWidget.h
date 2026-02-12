// Grid inventory widget. Uses Model/View pattern with UFlecsContainerModel.
// Inherits UFlecsUIPanel for CommonUI-native input/cursor management.
//
// Two usage modes:
//   1. Pure C++ (default): BuildDefaultWidgetTree() auto-creates Border + CanvasPanel.
//   2. Blueprint Designer: Create WBP_Inventory child class in editor.
//      Add Border ("BackgroundBorder") + CanvasPanel ("GridCanvas") in Designer.
//      BindWidgetOptional links them automatically. C++ skips auto-creation.
//
// Slot/Item sub-widgets can also be Blueprint subclasses via SlotWidgetClass/ItemWidgetClass.

#pragma once

#include "CoreMinimal.h"
#include "FlecsUIPanel.h"
#include "IFlecsContainerView.h"
#include "FlecsContainerTypes.h"
#include "FlecsInventoryWidget.generated.h"

class AFlecsCharacter;
class UBorder;
class UCanvasPanel;
class UFlecsInventorySlotWidget;
class UFlecsInventoryItemWidget;
class UFlecsContainerModel;

UCLASS()
class FATUMGAME_API UFlecsInventoryWidget : public UFlecsUIPanel, public IFlecsContainerView
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// PUBLIC API (called by AFlecsCharacter)
	// ═══════════════════════════════════════════════════════════════

	void OpenInventory(int64 InContainerEntityId);
	void CloseInventory();
	bool IsInventoryOpen() const { return bIsOpen; }
	void SetOwningCharacter(AFlecsCharacter* InCharacter);

	// ═══════════════════════════════════════════════════════════════
	// PUBLIC (used by slot/item widgets)
	// ═══════════════════════════════════════════════════════════════

	void RequestMoveItem(int64 ItemEntityId, FIntPoint NewGridPosition);
	bool CanFitAt(int64 ItemEntityId, FIntPoint Position) const;
	bool CanStackAt(int64 ItemEntityId, FIntPoint Position) const;
	int32 GetGridWidth() const;
	int32 GetGridHeight() const;

	// ═══════════════════════════════════════════════════════════════
	// IFlecsContainerView
	// ═══════════════════════════════════════════════════════════════

	virtual void OnContainerSnapshotReceived(const FContainerSnapshot& Snapshot) override;
	virtual void OnItemMoved(int64 ItemEntityId, FIntPoint OldPos, FIntPoint NewPos) override;
	virtual void OnOperationRolledBack(uint32 OpId) override;

protected:
	virtual void BuildDefaultWidgetTree() override;
	virtual void PostInitialize() override;

	// ═══════════════════════════════════════════════════════════════
	// VIRTUAL — override in C++ or BP subclass for custom visuals
	// ═══════════════════════════════════════════════════════════════

	virtual void BuildGrid(int32 InGridWidth, int32 InGridHeight);
	virtual void PopulateItems(const TArray<FContainerItemSnapshot>& Items);
	virtual void ClearAll();

	// ═══════════════════════════════════════════════════════════════
	// DESIGNER-BINDABLE WIDGETS (BindWidgetOptional)
	// In BP child: add widgets with these names in Designer.
	// In pure C++: auto-created in Initialize().
	// ═══════════════════════════════════════════════════════════════

	/** Background border. Name in Designer: "BackgroundBorder". */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BackgroundBorder;

	/** Canvas for grid slots and items. Name in Designer: "GridCanvas". */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> GridCanvas;

	// ═══════════════════════════════════════════════════════════════
	// VISUAL CONFIGURATION (editable in Details panel)
	// ═══════════════════════════════════════════════════════════════

	/** Size of each grid cell in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Grid")
	float CellSize = 50.f;

	/** Background color (only used when C++ auto-creates the border). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Grid")
	FLinearColor BackgroundColor = FLinearColor(0.02f, 0.02f, 0.05f, 0.92f);

	/** Padding inside the background border (only used when C++ auto-creates). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Grid")
	float BackgroundPadding = 8.f;

	/** Widget class to use for grid slots. Override with BP subclass for custom visuals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Widget Classes")
	TSubclassOf<UFlecsInventorySlotWidget> SlotWidgetClass;

	/** Widget class to use for item visuals. Override with BP subclass for custom visuals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Widget Classes")
	TSubclassOf<UFlecsInventoryItemWidget> ItemWidgetClass;

private:
	// ═══════════════════════════════════════════════════════════════
	// DATA
	// ═══════════════════════════════════════════════════════════════

	bool bIsOpen = false;
	int64 ContainerEntityId = 0;

	UPROPERTY()
	TObjectPtr<UFlecsContainerModel> ContainerModel;

	UPROPERTY()
	TArray<TObjectPtr<UFlecsInventorySlotWidget>> SlotWidgets;

	UPROPERTY()
	TArray<TObjectPtr<UFlecsInventoryItemWidget>> ItemWidgets;

	TWeakObjectPtr<AFlecsCharacter> OwningCharacter;
};
