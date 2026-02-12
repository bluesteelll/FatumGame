// Grid inventory widget. Builds slot grid + item visuals entirely in C++.
// No Blueprint child or Designer setup needed.
//
// Implements IFlecsContainerView to receive snapshots from UFlecsContainerModel.
// All container data flows through the Model (lock-free triple buffer from sim thread).
//
// Usage: set InventoryWidgetClass = UFlecsInventoryWidget in BP_Player.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "IFlecsContainerView.h"
#include "FlecsContainerTypes.h"
#include "FlecsInventoryWidget.generated.h"

class AFlecsCharacter;
class UBorder;
class UCanvasPanel;
class UFlecsInventorySlotWidget;
class UFlecsInventoryItemWidget;
class UFlecsContainerModel;

UCLASS(Blueprintable)
class FATUMGAME_API UFlecsInventoryWidget : public UUserWidget, public IFlecsContainerView
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
	virtual bool Initialize() override;

	// ═══════════════════════════════════════════════════════════════
	// VIRTUAL — override in C++ subclass for custom visuals
	// ═══════════════════════════════════════════════════════════════

	virtual void BuildGrid(int32 GridWidth, int32 GridHeight);
	virtual void PopulateItems(const TArray<FContainerItemSnapshot>& Items);
	virtual void ClearAll();

	UPROPERTY()
	TObjectPtr<UCanvasPanel> GridCanvas;

	float CellSize = 50.f;

private:
	// ═══════════════════════════════════════════════════════════════
	// DATA
	// ═══════════════════════════════════════════════════════════════

	bool bIsOpen = false;
	int64 ContainerEntityId = 0;

	UPROPERTY()
	TObjectPtr<UFlecsContainerModel> ContainerModel;

	UPROPERTY()
	TObjectPtr<UBorder> BackgroundBorder;

	UPROPERTY()
	TArray<TObjectPtr<UFlecsInventorySlotWidget>> SlotWidgets;

	UPROPERTY()
	TArray<TObjectPtr<UFlecsInventoryItemWidget>> ItemWidgets;

	TWeakObjectPtr<AFlecsCharacter> OwningCharacter;
};
