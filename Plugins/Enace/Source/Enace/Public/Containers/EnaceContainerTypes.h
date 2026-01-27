// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "SkeletonTypes.h"
#include "EnaceContainerTypes.generated.h"

class UEnaceItemDefinition;

// ═══════════════════════════════════════════════════════════════════════════
// CONTAINER SLOT
// ═══════════════════════════════════════════════════════════════════════════

/**
 * A single slot within a container.
 * Stores a copy of item data (hybrid model - items "disappear" from world).
 */
USTRUCT(BlueprintType)
struct ENACE_API FEnaceContainerSlot
{
	GENERATED_BODY()

	/** Item definition (null = empty slot) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UEnaceItemDefinition> ItemDefinition = nullptr;

	/** Number of items in this slot */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
	int32 Count = 0;

	/**
	 * Type filter for this slot (optional).
	 * If set, only items with matching tag can be placed here.
	 * Example: "Enace.Category.Weapon" restricts slot to weapons only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag SlotTypeFilter;

	/** Whether this slot is locked (cannot add/remove items) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsLocked = false;

	/** Is this slot empty? */
	bool IsEmpty() const { return ItemDefinition == nullptr || Count <= 0; }

	/** Clear the slot */
	void Clear()
	{
		ItemDefinition = nullptr;
		Count = 0;
	}

	/** Check if an item can be placed in this slot (respects type filter) */
	bool CanAcceptItem(const UEnaceItemDefinition* Definition) const;

	/** Get remaining space for stacking */
	int32 GetRemainingSpace() const;
};

// ═══════════════════════════════════════════════════════════════════════════
// CONTAINER DATA
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Container data stored in EnaceDispatch.
 * Represents inventory, chest, bag, etc.
 */
USTRUCT(BlueprintType)
struct ENACE_API FEnaceContainerData
{
	GENERATED_BODY()

	/** All slots in this container */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FEnaceContainerSlot> Slots;

	/**
	 * Maximum number of slots.
	 * -1 = dynamic (can add slots freely)
	 * >0 = fixed (cannot exceed this limit)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 MaxSlots = -1;

	/** Owner of this container (Actor, Item, or invalid for standalone) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FSkeletonKey OwnerKey;

	/** Container definition (if this container is an item itself) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UEnaceItemDefinition> ContainerDefinition = nullptr;

	/** Allow nested containers (bags inside chests) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bAllowNesting = true;

	/** Get number of used (non-empty) slots */
	int32 GetUsedSlotCount() const;

	/** Get number of empty slots */
	int32 GetEmptySlotCount() const;

	/** Check if container is full */
	bool IsFull() const;

	/** Find first empty slot index (-1 if none) */
	int32 FindFirstEmptySlot() const;

	/** Find slot containing specific item type (-1 if none) */
	int32 FindItemSlot(const UEnaceItemDefinition* Definition) const;

	/** Find slot with room to stack more of this item (-1 if none) */
	int32 FindStackableSlot(const UEnaceItemDefinition* Definition) const;
};

// ═══════════════════════════════════════════════════════════════════════════
// OPERATION RESULTS
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Result of adding an item to a container
 */
USTRUCT(BlueprintType)
struct ENACE_API FEnaceAddItemResult
{
	GENERATED_BODY()

	/** Operation succeeded (at least partially) */
	UPROPERTY(BlueprintReadOnly)
	bool bSuccess = false;

	/** Number of items actually added */
	UPROPERTY(BlueprintReadOnly)
	int32 AddedCount = 0;

	/** Number of items that couldn't fit (overflow) */
	UPROPERTY(BlueprintReadOnly)
	int32 OverflowCount = 0;

	/** Slot index where items were added (-1 if spread across multiple) */
	UPROPERTY(BlueprintReadOnly)
	int32 SlotIndex = -1;
};

/**
 * Result of moving items between containers/slots
 */
USTRUCT(BlueprintType)
struct ENACE_API FEnaceMoveItemResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	bool bSuccess = false;

	/** Items moved */
	UPROPERTY(BlueprintReadOnly)
	int32 MovedCount = 0;

	/** Items swapped (if destination had different item) */
	UPROPERTY(BlueprintReadOnly)
	bool bSwapped = false;
};

// ═══════════════════════════════════════════════════════════════════════════
// CONTAINER EVENTS
// ═══════════════════════════════════════════════════════════════════════════

/** Event type for container changes */
UENUM(BlueprintType)
enum class EEnaceContainerEventType : uint8
{
	ItemAdded,
	ItemRemoved,
	ItemMoved,
	SlotChanged,
	ContainerCreated,
	ContainerDestroyed
};

/**
 * Container change event data
 */
USTRUCT(BlueprintType)
struct ENACE_API FEnaceContainerEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EEnaceContainerEventType EventType = EEnaceContainerEventType::SlotChanged;

	UPROPERTY(BlueprintReadOnly)
	FSkeletonKey ContainerKey;

	UPROPERTY(BlueprintReadOnly)
	int32 SlotIndex = -1;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UEnaceItemDefinition> ItemDefinition = nullptr;

	UPROPERTY(BlueprintReadOnly)
	int32 Count = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 PreviousCount = 0;
};

// Delegate for container events (broadcast on game thread for UI safety)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnaceContainerChanged, const FEnaceContainerEvent&, Event);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEnaceContainerChangedNative, const FEnaceContainerEvent& /*Event*/);
