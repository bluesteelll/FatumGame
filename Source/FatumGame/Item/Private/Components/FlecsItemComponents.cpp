// Item/Container component implementations.

#include "FlecsItemComponents.h"
#include "FlecsItemDefinition.h"
#include "FlecsEntityDefinition.h"
#include "FlecsContainerProfile.h"
#include "FlecsMagazineProfile.h"
#include "FlecsQuickLoadProfile.h"
#include "FlecsAmmoTypeDefinition.h"
#include "FlecsCaliberRegistry.h"
#include "Properties/FlecsComponentProperties.h"

REGISTER_FLECS_COMPONENT(FItemInstance);
REGISTER_FLECS_COMPONENT(FItemUniqueData);
REGISTER_FLECS_COMPONENT(FContainerInstance);
REGISTER_FLECS_COMPONENT(FContainerGridInstance);
REGISTER_FLECS_COMPONENT(FContainerSlotsInstance);
REGISTER_FLECS_COMPONENT(FWorldItemInstance);
REGISTER_FLECS_COMPONENT(FContainedIn);

// ═══════════════════════════════════════════════════════════════
// ITEM
// ═══════════════════════════════════════════════════════════════

FItemStaticData FItemStaticData::FromProfile(UFlecsItemDefinition* ItemDef, UFlecsEntityDefinition* EntityDef)
{
	check(ItemDef);

	FItemStaticData S;
	S.TypeId = ItemDef->ItemTypeId != 0 ? ItemDef->ItemTypeId : GetTypeHash(ItemDef->ItemName);
	S.MaxStack = ItemDef->MaxStackSize;
	S.Weight = ItemDef->Weight;
	S.GridSize = ItemDef->GridSize;
	S.ItemName = ItemDef->ItemName;
	S.EntityDefinition = EntityDef;
	S.ItemDefinition = ItemDef;
	return S;
}

// ═══════════════════════════════════════════════════════════════
// CONTAINER
// ═══════════════════════════════════════════════════════════════

FContainerStatic FContainerStatic::FromProfile(const UFlecsContainerProfile* Profile)
{
	check(Profile);

	FContainerStatic S;
	S.Type = Profile->ContainerType;
	S.MaxWeight = Profile->MaxWeight;
	S.bAllowNesting = Profile->bAllowNestedContainers;
	S.bAutoStack = Profile->bAutoStackOnAdd;

	if (Profile->ContainerType == EContainerType::Slot)
	{
		// Slot containers: map slots to a Nx1 grid for UI compatibility
		S.GridWidth = FMath::Max(1, Profile->Slots.Num());
		S.GridHeight = 1;
		S.MaxItems = Profile->Slots.Num();
	}
	else
	{
		S.GridWidth = Profile->GridWidth;
		S.GridHeight = Profile->GridHeight;
		S.MaxItems = Profile->MaxListItems;
	}

	return S;
}

// ═══════════════════════════════════════════════════════════════
// GRID CONTAINER INSTANCE
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

// ═══════════════════════════════════════════════════════════════
// MAGAZINE
// ═══════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════
// QUICK-LOAD DEVICE
// ═══════════════════════════════════════════════════════════════

FQuickLoadStatic FQuickLoadStatic::FromProfile(const UFlecsQuickLoadProfile* Profile, const UFlecsCaliberRegistry* CaliberRegistry)
{
	check(Profile);

	static_assert(static_cast<uint8>(EQuickLoadDeviceTypeUI::StripperClip) == static_cast<uint8>(EQuickLoadDeviceType::StripperClip));
	static_assert(static_cast<uint8>(EQuickLoadDeviceTypeUI::Speedloader) == static_cast<uint8>(EQuickLoadDeviceType::Speedloader));

	FQuickLoadStatic S;
	S.DeviceType = static_cast<EQuickLoadDeviceType>(Profile->DeviceType);
	S.RoundsHeld = Profile->RoundsHeld;
	S.CaliberId = CaliberRegistry ? CaliberRegistry->GetCaliberIndex(Profile->Caliber) : 0xFF;
	if (S.CaliberId == 0xFF)
	{
		UE_LOG(LogTemp, Error, TEXT("QuickLoadProfile: caliber '%s' not found in CaliberRegistry — device will not work!"),
			*Profile->Caliber.ToString());
	}
	S.AmmoTypeDefinition = Profile->AmmoTypeDefinition;
	if (!S.AmmoTypeDefinition)
	{
		UE_LOG(LogTemp, Error, TEXT("QuickLoadProfile: AmmoTypeDefinition is null — device will not work!"));
	}
	S.InsertTime = Profile->InsertTime;
	S.bRequiresEmptyMagazine = Profile->bRequiresEmptyMagazine;
	return S;
}

// ═══════════════════════════════════════════════════════════════
// MAGAZINE
// ═══════════════════════════════════════════════════════════════

FMagazineStatic FMagazineStatic::FromProfile(const UFlecsMagazineProfile* Profile, const UFlecsCaliberRegistry* CaliberRegistry)
{
	check(Profile);

	FMagazineStatic S;
	S.CaliberId = CaliberRegistry ? CaliberRegistry->GetCaliberIndex(Profile->Caliber) : 0xFF;
	checkf(S.CaliberId != 0xFF, TEXT("Magazine caliber '%s' not found in CaliberRegistry"), *Profile->Caliber.ToString());
	S.Capacity = FMath::Clamp(Profile->Capacity, 1, MAX_MAGAZINE_CAPACITY);
	S.WeightPerRound = Profile->WeightPerRound;
	S.ReloadSpeedModifier = Profile->ReloadSpeedModifier;

	S.AcceptedAmmoTypeCount = FMath::Min(Profile->AcceptedAmmoTypes.Num(), MAX_MAGAZINE_AMMO_TYPES);
	for (int32 i = 0; i < S.AcceptedAmmoTypeCount; ++i)
	{
		S.AcceptedAmmoTypes[i] = Profile->AcceptedAmmoTypes[i];
	}

	return S;
}
