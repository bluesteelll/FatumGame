// UFlecsMultiblockBlueprint — IsDataValid implementation.

#include "FlecsMultiblockBlueprint.h"
#include "FlecsEntityDefinition.h"
#include "FlecsCraftingStationProfile.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_EDITOR
EDataValidationResult UFlecsMultiblockBlueprint::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	auto Err = [&](const TCHAR* Msg)
	{
		Context.AddError(FText::FromString(Msg));
		Result = EDataValidationResult::Invalid;
	};

	if (BlueprintId.IsNone())
	{
		Err(TEXT("BlueprintId is None"));
	}

	if (!AnchorPartDefinition)
	{
		Err(TEXT("AnchorPartDefinition null"));
	}

	if (AnchorPartDefinition && AnchorPartDefinition->CraftingStationProfile)
	{
		Err(TEXT("Anchor part MUST NOT have CraftingStationProfile (conflict — upgrade path handles it)"));
	}

	if (!StationProfile)
	{
		Err(TEXT("StationProfile null"));
	}

	if (Children.Num() < 1)
	{
		Err(TEXT("Children empty"));
	}

	if (Children.Num() > 15)
	{
		Err(TEXT("Children exceed 15"));
	}

	float MaxOffset = 0.f;
	float MaxTol = 0.f;

	for (int32 i = 0; i < Children.Num(); ++i)
	{
		const FMultiblockChildPartSpec& C = Children[i];

		if (!C.PartDefinition)
		{
			Err(*FString::Printf(TEXT("Children[%d].PartDefinition null"), i));
		}

		if (C.PartRole.IsNone())
		{
			Err(*FString::Printf(TEXT("Children[%d].PartRole is None"), i));
		}

		if (C.PositionTolerance <= 0.f)
		{
			Err(*FString::Printf(TEXT("Children[%d].PositionTolerance must be > 0"), i));
		}

		MaxOffset = FMath::Max(MaxOffset, C.RelativeOffset.Size());
		MaxTol = FMath::Max(MaxTol, C.PositionTolerance);
	}

	const float MinRadius = MaxOffset + MaxTol + 10.f;
	if (DetectionScanRadius < MinRadius)
	{
		Err(*FString::Printf(
			TEXT("DetectionScanRadius %.1f < required %.1f (maxOffset=%.1f + maxTol=%.1f + 10cm margin)"),
			DetectionScanRadius, MinRadius, MaxOffset, MaxTol));
	}

	return Result;
}
#endif // WITH_EDITOR
