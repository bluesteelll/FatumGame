// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "Containers/EnaceContainerTypes.h"
#include "Items/EnaceItemDefinition.h"

// ═══════════════════════════════════════════════════════════════════════════
// FEnaceContainerSlot
// ═══════════════════════════════════════════════════════════════════════════

bool FEnaceContainerSlot::CanAcceptItem(const UEnaceItemDefinition* Definition) const
{
	if (bIsLocked)
	{
		return false;
	}

	if (!Definition)
	{
		return false;
	}

	// Check type filter
	if (SlotTypeFilter.IsValid())
	{
		if (!Definition->ItemTags.HasTag(SlotTypeFilter))
		{
			return false;
		}
	}

	return true;
}

int32 FEnaceContainerSlot::GetRemainingSpace() const
{
	if (IsEmpty())
	{
		return 0;  // Empty slot has no "remaining" space for stacking
	}

	if (!ItemDefinition)
	{
		return 0;
	}

	return FMath::Max(0, ItemDefinition->MaxStackSize - Count);
}

// ═══════════════════════════════════════════════════════════════════════════
// FEnaceContainerData
// ═══════════════════════════════════════════════════════════════════════════

int32 FEnaceContainerData::GetUsedSlotCount() const
{
	int32 Used = 0;
	for (const FEnaceContainerSlot& Slot : Slots)
	{
		if (!Slot.IsEmpty())
		{
			Used++;
		}
	}
	return Used;
}

int32 FEnaceContainerData::GetEmptySlotCount() const
{
	int32 Empty = 0;
	for (const FEnaceContainerSlot& Slot : Slots)
	{
		if (Slot.IsEmpty())
		{
			Empty++;
		}
	}
	return Empty;
}

bool FEnaceContainerData::IsFull() const
{
	// Dynamic container is never "full"
	if (MaxSlots < 0)
	{
		return false;
	}

	// Check if all slots are non-empty
	for (const FEnaceContainerSlot& Slot : Slots)
	{
		if (Slot.IsEmpty())
		{
			return false;
		}
	}

	// Also check if we can add more slots
	return Slots.Num() >= MaxSlots;
}

int32 FEnaceContainerData::FindFirstEmptySlot() const
{
	for (int32 i = 0; i < Slots.Num(); i++)
	{
		if (Slots[i].IsEmpty())
		{
			return i;
		}
	}
	return -1;
}

int32 FEnaceContainerData::FindItemSlot(const UEnaceItemDefinition* Definition) const
{
	if (!Definition)
	{
		return -1;
	}

	for (int32 i = 0; i < Slots.Num(); i++)
	{
		if (Slots[i].ItemDefinition == Definition && !Slots[i].IsEmpty())
		{
			return i;
		}
	}
	return -1;
}

int32 FEnaceContainerData::FindStackableSlot(const UEnaceItemDefinition* Definition) const
{
	if (!Definition)
	{
		return -1;
	}

	for (int32 i = 0; i < Slots.Num(); i++)
	{
		const FEnaceContainerSlot& Slot = Slots[i];
		if (Slot.ItemDefinition == Definition &&
			!Slot.IsEmpty() &&
			Slot.GetRemainingSpace() > 0)
		{
			return i;
		}
	}
	return -1;
}
