// UFlecsCraftingStationProfile — IsDataValid implementation.

#include "FlecsCraftingStationProfile.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_EDITOR
EDataValidationResult UFlecsCraftingStationProfile::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (SlotLayout.Num() == 0)
	{
		Context.AddError(FText::FromString(TEXT("SlotLayout is empty — at least one slot required.")));
		Result = EDataValidationResult::Invalid;
	}

	if (SlotLayout.Num() > kMaxCraftingSlots)
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("SlotLayout has %d slots; max is %d (kMaxCraftingSlots)."),
			SlotLayout.Num(), kMaxCraftingSlots)));
		Result = EDataValidationResult::Invalid;
	}

	int32 MaterialInputCount = 0;
	int32 FuelCount = 0;
	int32 OutputCount = 0;

	for (int32 i = 0; i < SlotLayout.Num(); ++i)
	{
		const FSlotLayoutDef& Slot = SlotLayout[i];
		if (!Slot.ContainerProfile)
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Slot %d ('%s') has null ContainerProfile."), i, *Slot.SlotName.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		switch (Slot.Role)
		{
		case ESlotRole::MaterialInput: ++MaterialInputCount; break;
		case ESlotRole::Fuel:          ++FuelCount;          break;
		case ESlotRole::Output:        ++OutputCount;        break;
		default:                                              break;
		}
	}

	if (MaterialInputCount < 1)
	{
		Context.AddError(FText::FromString(TEXT("SlotLayout requires >= 1 MaterialInput slot.")));
		Result = EDataValidationResult::Invalid;
	}
	if (FuelCount > 1)
	{
		Context.AddError(FText::FromString(TEXT("SlotLayout allows at most 1 Fuel slot.")));
		Result = EDataValidationResult::Invalid;
	}
	if (OutputCount < 1)
	{
		Context.AddError(FText::FromString(TEXT("SlotLayout requires >= 1 Output slot.")));
		Result = EDataValidationResult::Invalid;
	}

	const bool bHasFuelSlot = (FuelCount == 1);
	const bool bHasFuelTypes = !SupportedFuelTypes.IsEmpty();
	if (bHasFuelSlot != bHasFuelTypes)
	{
		Context.AddError(FText::FromString(
			TEXT("SupportedFuelTypes must be non-empty iff SlotLayout contains a Fuel slot.")));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif // WITH_EDITOR
