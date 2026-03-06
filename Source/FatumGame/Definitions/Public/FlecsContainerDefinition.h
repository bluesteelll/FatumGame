// Data Asset for Flecs-based containers.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FlecsGameTags.h"
#include "FlecsContainerDefinition.generated.h"

/**
 * Named slot definition for equipment containers.
 */
USTRUCT(BlueprintType)
struct FNamedSlotDefinition
{
	GENERATED_BODY()

	/** Unique slot ID within this container */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slot")
	int32 SlotId = 0;

	/** Display name for UI */
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
 * Data Asset defining a container type.
 * Create in Content Browser: Right Click -> Miscellaneous -> Data Asset -> FlecsContainerDefinition
 *
 * Container Types:
 * - Grid: 2D inventory (Diablo/Tarkov style)
 * - Slot: Named equipment slots (Head, Chest, etc.)
 * - List: Simple item list without positioning
 */
UCLASS(BlueprintType)
class FATUMGAME_API UFlecsContainerDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// IDENTITY
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Unique container definition ID. Used for registry lookup.
	 * MUST be unique across all containers. 0 = invalid.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (ClampMin = "1"))
	int32 DefinitionId = 0;

	/** Internal name for this container type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FName ContainerName = "NewContainer";

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

	int32 GetGridCellCount() const { return GridWidth * GridHeight; }

	// ═══════════════════════════════════════════════════════════════
	// SLOT SETTINGS (ContainerType == Slot)
	// ═══════════════════════════════════════════════════════════════

	/** Named slots for equipment containers */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slots", meta = (EditCondition = "ContainerType == EContainerType::Slot"))
	TArray<FNamedSlotDefinition> NamedSlots;

	const FNamedSlotDefinition* FindSlotDefinition(int32 SlotId) const
	{
		for (const FNamedSlotDefinition& Slot : NamedSlots)
		{
			if (Slot.SlotId == SlotId)
			{
				return &Slot;
			}
		}
		return nullptr;
	}

	const FNamedSlotDefinition* FindSlotDefinitionByName(FName Name) const
	{
		for (const FNamedSlotDefinition& Slot : NamedSlots)
		{
			if (Slot.SlotName == Name)
			{
				return &Slot;
			}
		}
		return nullptr;
	}

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

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId("FlecsContainerDefinition", ContainerName);
	}

	/** Check if an item (by tags) can be placed in this container */
	bool CanAcceptItem(const FGameplayTagContainer& ItemTags) const
	{
		if (AllowedItemFilter.IsEmpty())
		{
			return true;
		}
		return AllowedItemFilter.Matches(ItemTags);
	}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);

		// Auto-generate DefinitionId if not set
		if (DefinitionId == 0 && !ContainerName.IsNone())
		{
			DefinitionId = GetTypeHash(ContainerName);
		}
	}
#endif
};
