// Container profile for Flecs entity spawning.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FlecsGameTags.h"
#include "FlecsContainerProfile.generated.h"

class UTexture2D;

/**
 * Named slot definition for equipment containers.
 */
USTRUCT(BlueprintType)
struct FContainerSlotDefinition
{
	GENERATED_BODY()

	/** Unique slot ID within this container */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slot")
	int32 SlotId = 0;

	/** Internal slot name */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slot")
	FName SlotName;

	/** Display text for UI */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slot")
	FText DisplayName;

	/** Only items with this tag can go in this slot (empty = any) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slot")
	FGameplayTag SlotFilter;

	/** Icon for empty slot in UI */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slot")
	TObjectPtr<UTexture2D> EmptySlotIcon;
};

/**
 * Data Asset defining container properties for entity spawning.
 *
 * Used with FEntitySpawnRequest to make an entity act as a container.
 * Containers can hold items (FItemInstance entities).
 *
 * Container Types:
 * - Grid: 2D inventory (Diablo/Tarkov style) - items occupy WxH cells
 * - Slot: Named equipment slots (Head, Chest, Weapon) - filtered by tags
 * - List: Simple item list without positioning - fastest, no grid UI
 *
 * Note: A chest in the world = ContainerProfile + PhysicsProfile + RenderProfile
 *       Player inventory = ContainerProfile only (no world presence)
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsContainerProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// IDENTITY
	// ═══════════════════════════════════════════════════════════════

	/** Internal name for this container type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FName ContainerName = "Container";

	/** Display name shown in UI */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FText DisplayName;

	// ═══════════════════════════════════════════════════════════════
	// TYPE
	// ═══════════════════════════════════════════════════════════════

	/** Container layout type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Type")
	EContainerType ContainerType = EContainerType::Grid;

	// ═══════════════════════════════════════════════════════════════
	// GRID SETTINGS (ContainerType == Grid)
	// ═══════════════════════════════════════════════════════════════

	/** Grid width in cells */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid", meta = (EditCondition = "ContainerType == EContainerType::Grid", ClampMin = "1", ClampMax = "20"))
	int32 GridWidth = 10;

	/** Grid height in cells */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid", meta = (EditCondition = "ContainerType == EContainerType::Grid", ClampMin = "1", ClampMax = "20"))
	int32 GridHeight = 4;

	// ═══════════════════════════════════════════════════════════════
	// SLOT SETTINGS (ContainerType == Slot)
	// ═══════════════════════════════════════════════════════════════

	/** Named slots for equipment containers */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slots", meta = (EditCondition = "ContainerType == EContainerType::Slot"))
	TArray<FContainerSlotDefinition> Slots;

	// ═══════════════════════════════════════════════════════════════
	// LIST SETTINGS (ContainerType == List)
	// ═══════════════════════════════════════════════════════════════

	/** Maximum items for list container (-1 = unlimited) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List", meta = (EditCondition = "ContainerType == EContainerType::List"))
	int32 MaxListItems = -1;

	// ═══════════════════════════════════════════════════════════════
	// RESTRICTIONS
	// ═══════════════════════════════════════════════════════════════

	/** Only allow items that match this query (empty = allow all) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Restrictions")
	FGameplayTagQuery AllowedItemFilter;

	/** Maximum total weight (-1 = unlimited) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Restrictions")
	float MaxWeight = -1.f;

	/** Can this container hold other containers? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Restrictions")
	bool bAllowNestedContainers = false;

	// ═══════════════════════════════════════════════════════════════
	// BEHAVIOR
	// ═══════════════════════════════════════════════════════════════

	/** Auto-stack items when adding? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bAutoStackOnAdd = true;

	/** Auto-sort items? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bAutoSort = false;

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	int32 GetGridCellCount() const { return GridWidth * GridHeight; }
	int32 GetSlotCount() const { return Slots.Num(); }

	const FContainerSlotDefinition* FindSlot(int32 SlotId) const
	{
		for (const FContainerSlotDefinition& Slot : Slots)
		{
			if (Slot.SlotId == SlotId) return &Slot;
		}
		return nullptr;
	}

	const FContainerSlotDefinition* FindSlotByName(FName Name) const
	{
		for (const FContainerSlotDefinition& Slot : Slots)
		{
			if (Slot.SlotName == Name) return &Slot;
		}
		return nullptr;
	}

	bool CanAcceptItem(const FGameplayTagContainer& ItemTags) const
	{
		return AllowedItemFilter.IsEmpty() || AllowedItemFilter.Matches(ItemTags);
	}
};
