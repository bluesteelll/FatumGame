// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "FlecsComponents.h"
#include "Properties/FlecsComponentProperties.h"

// ═══════════════════════════════════════════════════════════════
// COMPONENT REGISTRATION
// Register all components with the Flecs ECS via Unreal-Flecs plugin.
// These register on FCoreDelegates::OnPostEngineInit automatically.
// ═══════════════════════════════════════════════════════════════

// Core gameplay components
REGISTER_FLECS_COMPONENT(FItemData);
REGISTER_FLECS_COMPONENT(FHealthData);
REGISTER_FLECS_COMPONENT(FDamageSource);
REGISTER_FLECS_COMPONENT(FLootData);

// Physics bridge components
REGISTER_FLECS_COMPONENT(FBarrageBody);
REGISTER_FLECS_COMPONENT(FISMRender);

// Container components
REGISTER_FLECS_COMPONENT(FContainerSlot);
REGISTER_FLECS_COMPONENT(FContainerData);

// Tags (zero-size, used for archetype queries)
REGISTER_FLECS_COMPONENT(FTagItem);
REGISTER_FLECS_COMPONENT(FTagDestructible);
REGISTER_FLECS_COMPONENT(FTagPickupable);
REGISTER_FLECS_COMPONENT(FTagHasLoot);
REGISTER_FLECS_COMPONENT(FTagDead);

// ═══════════════════════════════════════════════════════════════
// COMPONENT IMPLEMENTATIONS
// ═══════════════════════════════════════════════════════════════

int32 FContainerData::GetUsedSlotCount() const
{
	int32 Used = 0;
	for (const FContainerSlot& Slot : Slots)
	{
		if (!Slot.IsEmpty())
		{
			++Used;
		}
	}
	return Used;
}

int32 FContainerData::GetEmptySlotCount() const
{
	return Slots.Num() - GetUsedSlotCount();
}

bool FContainerData::IsFull() const
{
	if (MaxSlots > 0 && Slots.Num() >= MaxSlots)
	{
		return GetEmptySlotCount() == 0;
	}
	return false;
}

int32 FContainerData::FindFirstEmptySlot() const
{
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].IsEmpty())
		{
			return i;
		}
	}
	return -1;
}
