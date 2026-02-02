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

// Legacy container components (kept for compatibility)
REGISTER_FLECS_COMPONENT(FContainerSlot);
REGISTER_FLECS_COMPONENT(FContainerData);

// Advanced Item System components
REGISTER_FLECS_COMPONENT(FItemInstance);
REGISTER_FLECS_COMPONENT(FItemUniqueData);
REGISTER_FLECS_COMPONENT(FContainedIn);
REGISTER_FLECS_COMPONENT(FWorldItemData);
REGISTER_FLECS_COMPONENT(FContainerBase);
REGISTER_FLECS_COMPONENT(FContainerGridData);
REGISTER_FLECS_COMPONENT(FContainerSlotsData);
REGISTER_FLECS_COMPONENT(FContainerListData);

// Tags (zero-size, used for archetype queries)
REGISTER_FLECS_COMPONENT(FTagItem);
REGISTER_FLECS_COMPONENT(FTagDroppedItem);
REGISTER_FLECS_COMPONENT(FTagContainer);
REGISTER_FLECS_COMPONENT(FTagDestructible);
REGISTER_FLECS_COMPONENT(FTagPickupable);
REGISTER_FLECS_COMPONENT(FTagHasLoot);
REGISTER_FLECS_COMPONENT(FTagDead);
REGISTER_FLECS_COMPONENT(FTagProjectile);
REGISTER_FLECS_COMPONENT(FTagCharacter);
REGISTER_FLECS_COMPONENT(FTagEquipment);
REGISTER_FLECS_COMPONENT(FTagConsumable);

// Collision components
REGISTER_FLECS_COMPONENT(FFlecsCollisionEvent);

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

// ═══════════════════════════════════════════════════════════════
// GRID CONTAINER IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════

void FContainerGridData::Initialize(int32 InWidth, int32 InHeight)
{
	Width = InWidth;
	Height = InHeight;
	const int32 TotalCells = Width * Height;
	const int32 BytesNeeded = (TotalCells + 7) / 8;
	OccupancyMask.SetNumZeroed(BytesNeeded);
}

bool FContainerGridData::IsCellOccupied(int32 X, int32 Y) const
{
	if (X < 0 || X >= Width || Y < 0 || Y >= Height)
	{
		return true; // Out of bounds = occupied
	}
	const int32 Index = GetCellIndex(X, Y);
	const int32 ByteIndex = Index / 8;
	const int32 BitIndex = Index % 8;
	if (ByteIndex >= OccupancyMask.Num())
	{
		return true;
	}
	return (OccupancyMask[ByteIndex] & (1 << BitIndex)) != 0;
}

bool FContainerGridData::CanFit(FIntPoint Position, FIntPoint Size) const
{
	if (Position.X < 0 || Position.Y < 0)
	{
		return false;
	}
	if (Position.X + Size.X > Width || Position.Y + Size.Y > Height)
	{
		return false;
	}
	for (int32 Y = Position.Y; Y < Position.Y + Size.Y; ++Y)
	{
		for (int32 X = Position.X; X < Position.X + Size.X; ++X)
		{
			if (IsCellOccupied(X, Y))
			{
				return false;
			}
		}
	}
	return true;
}

void FContainerGridData::Occupy(FIntPoint Position, FIntPoint Size)
{
	for (int32 Y = Position.Y; Y < Position.Y + Size.Y; ++Y)
	{
		for (int32 X = Position.X; X < Position.X + Size.X; ++X)
		{
			const int32 Index = GetCellIndex(X, Y);
			const int32 ByteIndex = Index / 8;
			const int32 BitIndex = Index % 8;
			if (ByteIndex < OccupancyMask.Num())
			{
				OccupancyMask[ByteIndex] |= (1 << BitIndex);
			}
		}
	}
}

void FContainerGridData::Free(FIntPoint Position, FIntPoint Size)
{
	for (int32 Y = Position.Y; Y < Position.Y + Size.Y; ++Y)
	{
		for (int32 X = Position.X; X < Position.X + Size.X; ++X)
		{
			const int32 Index = GetCellIndex(X, Y);
			const int32 ByteIndex = Index / 8;
			const int32 BitIndex = Index % 8;
			if (ByteIndex < OccupancyMask.Num())
			{
				OccupancyMask[ByteIndex] &= ~(1 << BitIndex);
			}
		}
	}
}

FIntPoint FContainerGridData::FindFreeSpace(FIntPoint Size) const
{
	for (int32 Y = 0; Y <= Height - Size.Y; ++Y)
	{
		for (int32 X = 0; X <= Width - Size.X; ++X)
		{
			if (CanFit(FIntPoint(X, Y), Size))
			{
				return FIntPoint(X, Y);
			}
		}
	}
	return FIntPoint(-1, -1);
}

// ═══════════════════════════════════════════════════════════════
// SLOTS CONTAINER IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════

FEquipmentSlot* FContainerSlotsData::FindSlotById(int32 SlotId)
{
	for (FEquipmentSlot& Slot : Slots)
	{
		if (Slot.SlotId == SlotId)
		{
			return &Slot;
		}
	}
	return nullptr;
}

const FEquipmentSlot* FContainerSlotsData::FindSlotById(int32 SlotId) const
{
	for (const FEquipmentSlot& Slot : Slots)
	{
		if (Slot.SlotId == SlotId)
		{
			return &Slot;
		}
	}
	return nullptr;
}

FEquipmentSlot* FContainerSlotsData::FindSlotByName(FName Name)
{
	for (FEquipmentSlot& Slot : Slots)
	{
		if (Slot.SlotName == Name)
		{
			return &Slot;
		}
	}
	return nullptr;
}

int32 FContainerSlotsData::FindEmptySlotId() const
{
	for (const FEquipmentSlot& Slot : Slots)
	{
		if (Slot.IsEmpty())
		{
			return Slot.SlotId;
		}
	}
	return -1;
}

bool FContainerSlotsData::IsSlotEmpty(int32 SlotId) const
{
	const FEquipmentSlot* Slot = FindSlotById(SlotId);
	return Slot ? Slot->IsEmpty() : true;
}
