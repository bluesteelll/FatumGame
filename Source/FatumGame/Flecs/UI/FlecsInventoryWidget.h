// Grid inventory widget. Builds slot grid + item visuals entirely in C++.
// No Blueprint child or Designer setup needed.
//
// Usage: set InventoryWidgetClass = UFlecsInventoryWidget in BP_Player.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"
#include "FlecsInventoryWidget.generated.h"

class AFlecsCharacter;
class UBorder;
class UCanvasPanel;
class UFlecsInventorySlotWidget;
class UFlecsInventoryItemWidget;

UCLASS(Blueprintable)
class FATUMGAME_API UFlecsInventoryWidget : public UUserWidget
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
	int32 GetGridWidth() const { return CachedGridWidth; }
	int32 GetGridHeight() const { return CachedGridHeight; }

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ═══════════════════════════════════════════════════════════════
	// VIRTUAL — override in C++ subclass for custom visuals
	// ═══════════════════════════════════════════════════════════════

	virtual void BuildGrid(int32 GridWidth, int32 GridHeight);
	virtual void PopulateItems(const TArray<FInventoryItemSnapshot>& Items);
	virtual void ClearAll();

	UPROPERTY()
	TObjectPtr<UCanvasPanel> GridCanvas;

	float CellSize = 50.f;

private:
	// ═══════════════════════════════════════════════════════════════
	// MESSAGE HANDLERS
	// ═══════════════════════════════════════════════════════════════

	void HandleSnapshot(FGameplayTag Channel, const FUIInventorySnapshotMessage& Msg);
	void HandleChanged(FGameplayTag Channel, const FUIInventoryChangedMessage& Msg);
	void RequestSnapshot();

	// ═══════════════════════════════════════════════════════════════
	// LOCAL OCCUPANCY MIRROR (for instant drag preview)
	// ═══════════════════════════════════════════════════════════════

	void RebuildLocalOccupancy();
	bool LocalCanFit(FIntPoint Position, FIntPoint Size, int64 ExcludeItemEntityId) const;
	const FInventoryItemSnapshot* FindItemAtCell(FIntPoint Cell, int64 ExcludeItemEntityId) const;

	// ═══════════════════════════════════════════════════════════════
	// DATA
	// ═══════════════════════════════════════════════════════════════

	bool bIsOpen = false;
	int64 ContainerEntityId = 0;
	int32 CachedGridWidth = 0;
	int32 CachedGridHeight = 0;
	TArray<FInventoryItemSnapshot> CachedItems;

	/** Bit-packed local occupancy (mirrors FContainerGridInstance on game thread). */
	TArray<uint8> LocalOccupancyMask;

	UPROPERTY()
	TObjectPtr<UBorder> BackgroundBorder;

	UPROPERTY()
	TArray<TObjectPtr<UFlecsInventorySlotWidget>> SlotWidgets;

	UPROPERTY()
	TArray<TObjectPtr<UFlecsInventoryItemWidget>> ItemWidgets;

	FMessageListenerHandle SnapshotHandle;
	FMessageListenerHandle ChangedHandle;

	TWeakObjectPtr<AFlecsCharacter> OwningCharacter;
};
