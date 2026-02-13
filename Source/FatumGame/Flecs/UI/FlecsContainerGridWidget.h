// Reusable container grid sub-widget. Renders a grid of slots with drag-drop items.
// Implements IFlecsContainerView to receive model updates.
// Used inside UFlecsInventoryWidget (standalone) and UFlecsLootPanel (dual view).
//
// NOT a panel — no CommonUI activation overhead. Parent panel manages model lifecycle.

#pragma once

#include "CoreMinimal.h"
#include "FlecsUIWidget.h"
#include "IFlecsContainerView.h"
#include "FlecsContainerTypes.h"
#include "FlecsContainerGridWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UFlecsInventorySlotWidget;
class UFlecsInventoryItemWidget;
class UFlecsContainerModel;

UCLASS()
class FATUMGAME_API UFlecsContainerGridWidget : public UFlecsUIWidget, public IFlecsContainerView
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// BIND / UNBIND (called by parent panel)
	// ═══════════════════════════════════════════════════════════════

	void BindContainer(UFlecsContainerModel* InModel, int64 InContainerEntityId);
	void UnbindContainer();
	bool IsBound() const { return ContainerModel != nullptr; }
	int64 GetContainerEntityId() const { return ContainerEntityId; }

	// ═══════════════════════════════════════════════════════════════
	// API (used by slot/item widgets)
	// ═══════════════════════════════════════════════════════════════

	void RequestMoveItem(int64 ItemEntityId, FIntPoint NewGridPosition);
	bool CanFitAt(int64 ItemEntityId, FIntPoint Position) const;
	bool CanStackAt(int64 ItemEntityId, FIntPoint Position) const;

	/** Bounds + occupancy check WITHOUT item lookup. For cross-container highlight. */
	bool CanFitSizeAt(FIntPoint Position, FIntPoint Size) const;

	// ═══════════════════════════════════════════════════════════════
	// CROSS-CONTAINER CALLBACK (set by parent panel)
	// ═══════════════════════════════════════════════════════════════

	TFunction<void(int64 SourceContainerId, int64 ItemEntityId, FIntPoint DestPos)> OnCrossContainerDrop;

	// ═══════════════════════════════════════════════════════════════
	// IFlecsContainerView
	// ═══════════════════════════════════════════════════════════════

	virtual void OnContainerSnapshotReceived(const FContainerSnapshot& Snapshot) override;
	virtual void OnItemMoved(int64 ItemEntityId, FIntPoint OldPos, FIntPoint NewPos) override;
	virtual void OnOperationRolledBack(uint32 OpId) override;

	// ═══════════════════════════════════════════════════════════════
	// VISUAL CONFIGURATION (set by parent panel before BindContainer)
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container|Grid")
	float CellSize = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container|Grid")
	FLinearColor BackgroundColor = FLinearColor(0.02f, 0.02f, 0.05f, 0.92f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container|Grid")
	float BackgroundPadding = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container|Widget Classes")
	TSubclassOf<UFlecsInventorySlotWidget> SlotWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Container|Widget Classes")
	TSubclassOf<UFlecsInventoryItemWidget> ItemWidgetClass;

protected:
	virtual void BuildDefaultWidgetTree() override;
	virtual void PostInitialize() override;

	virtual void BuildGrid(int32 InGridWidth, int32 InGridHeight);
	virtual void PopulateItems(const TArray<FContainerItemSnapshot>& Items);
	virtual void ClearAll();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BackgroundBorder;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> GridCanvas;

private:
	int64 ContainerEntityId = 0;

	UPROPERTY()
	TObjectPtr<UFlecsContainerModel> ContainerModel;

	UPROPERTY()
	TArray<TObjectPtr<UFlecsInventorySlotWidget>> SlotWidgets;

	UPROPERTY()
	TArray<TObjectPtr<UFlecsInventoryItemWidget>> ItemWidgets;
};
