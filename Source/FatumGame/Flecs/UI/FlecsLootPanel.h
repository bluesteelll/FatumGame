// Side-by-side loot panel: player inventory (left) + external container (right).
// Uses two UFlecsContainerGridWidget instances with cross-container drag-drop.
// Manages two model lifecycles + CommonUI activation.

#pragma once

#include "CoreMinimal.h"
#include "FlecsUIPanel.h"
#include "FlecsLootPanel.generated.h"

class AFlecsCharacter;
class UFlecsContainerGridWidget;
class UFlecsContainerModel;
class UFlecsInventorySlotWidget;
class UFlecsInventoryItemWidget;
class UTextBlock;
class UButton;
class UVerticalBox;

UCLASS()
class FATUMGAME_API UFlecsLootPanel : public UFlecsUIPanel
{
	GENERATED_BODY()

public:
	void OpenLoot(int64 PlayerContainerId, int64 ExternalContainerId, const FText& ExternalTitle);
	void CloseLoot();
	bool IsLootOpen() const { return bIsOpen; }
	void SetOwningCharacter(AFlecsCharacter* InCharacter);

protected:
	virtual void BuildDefaultWidgetTree() override;
	virtual void PostInitialize() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot|Grid")
	float CellSize = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot|Widget Classes")
	TSubclassOf<UFlecsInventorySlotWidget> SlotWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot|Widget Classes")
	TSubclassOf<UFlecsInventoryItemWidget> ItemWidgetClass;

private:
	void HandleCrossContainerDrop(int64 SrcContainerId, int64 ItemEntityId, FIntPoint DestPos, int64 DestContainerId);

	UFUNCTION()
	void OnCloseButtonClicked();

	bool bIsOpen = false;
	int64 PlayerContainerEntityId = 0;
	int64 ExternalContainerEntityId = 0;

	UPROPERTY()
	TObjectPtr<UFlecsContainerGridWidget> PlayerGrid;

	UPROPERTY()
	TObjectPtr<UFlecsContainerGridWidget> ExternalGrid;

	UPROPERTY()
	TObjectPtr<UFlecsContainerModel> PlayerModel;

	UPROPERTY()
	TObjectPtr<UFlecsContainerModel> ExternalModel;

	UPROPERTY()
	TObjectPtr<UTextBlock> PlayerTitleText;

	UPROPERTY()
	TObjectPtr<UTextBlock> ExternalTitleText;

	UPROPERTY()
	TObjectPtr<UButton> CloseButton;

	// VBox containers — stored so PostInitialize can add grid widgets as children
	UPROPERTY()
	TObjectPtr<UVerticalBox> PlayerVBox;

	UPROPERTY()
	TObjectPtr<UVerticalBox> ExternalVBox;

	TWeakObjectPtr<AFlecsCharacter> OwningCharacter;
};
