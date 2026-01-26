// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "Items/EnaceItemTypes.h"
#include "Items/EnaceItemDefinition.h"

bool FEnaceItemStack::IsValid() const
{
	return Definition != nullptr && Count > 0;
}

bool FEnaceItemStack::CanStackWith(const FEnaceItemStack& Other) const
{
	if (!IsValid() || !Other.IsValid())
	{
		return false;
	}

	return Definition->CanStackWith(Other.Definition);
}

int32 FEnaceItemStack::GetMaxStackSize() const
{
	return Definition ? Definition->MaxStackSize : 1;
}

int32 FEnaceItemStack::GetRemainingSpace() const
{
	return FMath::Max(0, GetMaxStackSize() - Count);
}
