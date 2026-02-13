// Standalone inventory panel. Thin wrapper around UFlecsContainerGridWidget.
// Manages model lifecycle + CommonUI activation. Grid rendering delegated to inner widget.
//
// Two usage modes:
//   1. Pure C++ (default): BuildDefaultWidgetTree() auto-creates grid widget.
//   2. Blueprint Designer: Create WBP_Inventory child class in editor.

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

	void OpenInventory(int64 InContainerEntityId);
	void CloseInventory();
	bool IsInventoryOpen() const { return bIsOpen; }
	void SetOwningCharacter(AFlecsCharacter* InCharacter);

protected:
	virtual void BuildDefaultWidgetTree() override;
	virtual void PostInitialize() override;

	// ═══════════════════════════════════════════════════════════════
	// VISUAL CONFIGURATION (forwarded to inner grid)
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

	UPROPERTY()
	TObjectPtr<UFlecsContainerGridWidget> ContainerGrid;

	UPROPERTY()
	TObjectPtr<UFlecsContainerModel> ContainerModel;

	TWeakObjectPtr<AFlecsCharacter> OwningCharacter;
};
