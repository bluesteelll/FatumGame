// Standalone inventory panel. Thin wrapper around UFlecsContainerGridWidget.
// Manages model lifecycle + CommonUI activation. Grid rendering delegated to inner widget.
// Weapon slots displayed as a 2x1 grid alongside the main inventory grid.

#pragma once

#include "CoreMinimal.h"
#include "FlecsUIPanel.h"
#include "FlecsInventoryWidget.generated.h"

class AFlecsCharacter;
class UFlecsContainerGridWidget;
class UFlecsContainerModel;
class UFlecsInventorySlotWidget;
class UFlecsInventoryItemWidget;

UCLASS()
class FATUMGAME_API UFlecsInventoryWidget : public UFlecsUIPanel
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// PUBLIC API (called by AFlecsCharacter)
	// ═══════════════════════════════════════════════════════════════

	void OpenInventory(int64 InContainerEntityId, int64 InWeaponContainerEntityId = 0);
	void CloseInventory();
	bool IsInventoryOpen() const { return bIsOpen; }
	void SetOwningCharacter(AFlecsCharacter* InCharacter);

protected:
	virtual void BuildDefaultWidgetTree() override;
	virtual void PostInitialize() override;

	// ═══════════════════════════════════════════════════════════════
	// VISUAL CONFIGURATION (forwarded to inner grids)
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Grid")
	float CellSize = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Widget Classes")
	TSubclassOf<UFlecsInventorySlotWidget> SlotWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Widget Classes")
	TSubclassOf<UFlecsInventoryItemWidget> ItemWidgetClass;

private:
	bool bIsOpen = false;
	int64 ContainerEntityId = 0;
	int64 WeaponContainerEntityId = 0;

	UPROPERTY()
	TObjectPtr<UFlecsContainerGridWidget> ContainerGrid;

	UPROPERTY()
	TObjectPtr<UFlecsContainerGridWidget> WeaponSlotGrid;

	UPROPERTY()
	TObjectPtr<UFlecsContainerModel> ContainerModel;

	UPROPERTY()
	TObjectPtr<UFlecsContainerModel> WeaponSlotModel;

	TWeakObjectPtr<AFlecsCharacter> OwningCharacter;

	void HandleCrossContainerDrop(int64 SourceContainerId, int64 ItemEntityId, FIntPoint DestPos, int64 DestContainerId);
};
