// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "Items/EnaceItemDefinition.h"

bool UEnaceItemDefinition::CanStackWith(const UEnaceItemDefinition* Other) const
{
	if (!Other)
	{
		return false;
	}

	// Same item type and stackable
	return this == Other && MaxStackSize > 1;
}

FLinearColor UEnaceItemDefinition::GetDefaultRarityColor(EEnaceItemRarity InRarity)
{
	switch (InRarity)
	{
	case EEnaceItemRarity::Common:
		return FLinearColor(0.7f, 0.7f, 0.7f); // Gray
	case EEnaceItemRarity::Uncommon:
		return FLinearColor(0.2f, 0.8f, 0.2f); // Green
	case EEnaceItemRarity::Rare:
		return FLinearColor(0.2f, 0.4f, 1.0f); // Blue
	case EEnaceItemRarity::Epic:
		return FLinearColor(0.7f, 0.2f, 0.9f); // Purple
	case EEnaceItemRarity::Legendary:
		return FLinearColor(1.0f, 0.6f, 0.0f); // Orange
	default:
		return FLinearColor::White;
	}
}

FPrimaryAssetId UEnaceItemDefinition::GetPrimaryAssetId() const
{
	FName AssetName = ItemId.IsNone() ? GetFName() : ItemId;
	return FPrimaryAssetId(TEXT("EnaceItem"), AssetName);
}

#if WITH_EDITOR
void UEnaceItemDefinition::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Auto-set rarity color when rarity changes
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UEnaceItemDefinition, Rarity))
	{
		if (RarityColor == FLinearColor::White || RarityColor == GetDefaultRarityColor(EEnaceItemRarity::Common))
		{
			RarityColor = GetDefaultRarityColor(Rarity);
		}
	}

	// Auto-generate ItemId from asset name if empty
	if (ItemId.IsNone())
	{
		ItemId = GetFName();
	}
}
#endif
