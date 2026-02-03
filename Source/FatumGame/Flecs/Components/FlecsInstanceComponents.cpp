// Instance components implementation

#include "FlecsInstanceComponents.h"
#include "Properties/FlecsComponentProperties.h"

// ═══════════════════════════════════════════════════════════════
// COMPONENT REGISTRATION (Instance components)
// ═══════════════════════════════════════════════════════════════

REGISTER_FLECS_COMPONENT(FHealthInstance);
REGISTER_FLECS_COMPONENT(FProjectileInstance);
REGISTER_FLECS_COMPONENT(FItemInstance);
REGISTER_FLECS_COMPONENT(FItemUniqueData);
REGISTER_FLECS_COMPONENT(FContainerInstance);
REGISTER_FLECS_COMPONENT(FContainerGridInstance);
REGISTER_FLECS_COMPONENT(FContainerSlotsInstance);
REGISTER_FLECS_COMPONENT(FWorldItemInstance);
REGISTER_FLECS_COMPONENT(FContainedIn);

// ═══════════════════════════════════════════════════════════════
// GRID CONTAINER INSTANCE IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════

void FContainerGridInstance::Initialize(int32 Width, int32 Height)
{
	const int32 TotalCells = Width * Height;
	const int32 BytesNeeded = (TotalCells + 7) / 8;
	OccupancyMask.SetNumZeroed(BytesNeeded);
}

bool FContainerGridInstance::IsCellOccupied(int32 X, int32 Y, int32 Width) const
{
	const int32 Index = Y * Width + X;
	const int32 ByteIndex = Index / 8;
	const int32 BitIndex = Index % 8;
	if (ByteIndex >= OccupancyMask.Num())
	{
		return true; // Out of bounds = occupied
	}
	return (OccupancyMask[ByteIndex] & (1 << BitIndex)) != 0;
}

bool FContainerGridInstance::CanFit(FIntPoint Position, FIntPoint Size, int32 GridWidth, int32 GridHeight) const
{
	if (Position.X < 0 || Position.Y < 0)
	{
		return false;
	}
	if (Position.X + Size.X > GridWidth || Position.Y + Size.Y > GridHeight)
	{
		return false;
	}
	for (int32 Y = Position.Y; Y < Position.Y + Size.Y; ++Y)
	{
		for (int32 X = Position.X; X < Position.X + Size.X; ++X)
		{
			if (IsCellOccupied(X, Y, GridWidth))
			{
				return false;
			}
		}
	}
	return true;
}

void FContainerGridInstance::Occupy(FIntPoint Position, FIntPoint Size, int32 GridWidth)
{
	for (int32 Y = Position.Y; Y < Position.Y + Size.Y; ++Y)
	{
		for (int32 X = Position.X; X < Position.X + Size.X; ++X)
		{
			const int32 Index = Y * GridWidth + X;
			const int32 ByteIndex = Index / 8;
			const int32 BitIndex = Index % 8;
			if (ByteIndex < OccupancyMask.Num())
			{
				OccupancyMask[ByteIndex] |= (1 << BitIndex);
			}
		}
	}
}

void FContainerGridInstance::Free(FIntPoint Position, FIntPoint Size, int32 GridWidth)
{
	for (int32 Y = Position.Y; Y < Position.Y + Size.Y; ++Y)
	{
		for (int32 X = Position.X; X < Position.X + Size.X; ++X)
		{
			const int32 Index = Y * GridWidth + X;
			const int32 ByteIndex = Index / 8;
			const int32 BitIndex = Index % 8;
			if (ByteIndex < OccupancyMask.Num())
			{
				OccupancyMask[ByteIndex] &= ~(1 << BitIndex);
			}
		}
	}
}

FIntPoint FContainerGridInstance::FindFreeSpace(FIntPoint Size, int32 GridWidth, int32 GridHeight) const
{
	for (int32 Y = 0; Y <= GridHeight - Size.Y; ++Y)
	{
		for (int32 X = 0; X <= GridWidth - Size.X; ++X)
		{
			if (CanFit(FIntPoint(X, Y), Size, GridWidth, GridHeight))
			{
				return FIntPoint(X, Y);
			}
		}
	}
	return FIntPoint(-1, -1);
}
