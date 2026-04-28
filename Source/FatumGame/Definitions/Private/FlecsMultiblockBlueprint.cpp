// UFlecsMultiblockBlueprint — IsDataValid implementation.

#include "FlecsMultiblockBlueprint.h"
#include "FlecsEntityDefinition.h"
#include "FlecsCraftingStationProfile.h"
#include "FlecsCraftingTypes.h"

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

	// ─── Phase 4 — extension ports + sum cap (V2 PATCH 1) ─────────
	if (ExtensionPorts.Num() > 8)
	{
		Err(*FString::Printf(TEXT("ExtensionPorts.Num()=%d > 8 (designer cap)"), ExtensionPorts.Num()));
	}

	const int32 BaselineSlots = StationProfile ? StationProfile->SlotLayout.Num() : 0;
	if (BaselineSlots + ExtensionPorts.Num() > kMaxCraftingSlots)
	{
		Err(*FString::Printf(
			TEXT("BaselineSlots(%d) + ExtensionPorts(%d) = %d > kMaxCraftingSlots(%d)"),
			BaselineSlots, ExtensionPorts.Num(),
			BaselineSlots + ExtensionPorts.Num(), kMaxCraftingSlots));
	}

	bool bAnyDieSlot = false;
	bool bAnyOutputTray = false;
	bool bAnyToolRack = false;
	for (int32 i = 0; i < ExtensionPorts.Num(); ++i)
	{
		const FMultiblockExtensionPort& P = ExtensionPorts[i];
		if (P.PortType == EExtensionPortType::None)
		{
			Err(*FString::Printf(TEXT("ExtensionPorts[%d].PortType == None (sentinel — set a real type)"), i));
		}
		if (P.AcceptedPartRoles.Num() == 0)
		{
			Err(*FString::Printf(TEXT("ExtensionPorts[%d].AcceptedPartRoles is empty"), i));
		}
		if (P.MaxStackedAtThisPort < 1 || P.MaxStackedAtThisPort > 8)
		{
			Err(*FString::Printf(TEXT("ExtensionPorts[%d].MaxStackedAtThisPort=%u out of [1,8]"),
				i, static_cast<uint32>(P.MaxStackedAtThisPort)));
		}
		switch (P.PortType)
		{
		case EExtensionPortType::DieSlot:    bAnyDieSlot   = true; break;
		case EExtensionPortType::OutputTray: bAnyOutputTray = true; break;
		case EExtensionPortType::ToolRack:   bAnyToolRack  = true; break;
		default: break;
		}
	}

	if (StationProfile)
	{
		if (bAnyDieSlot && !StationProfile->ExtensionDieSlotProfile)
		{
			Err(TEXT("ExtensionPorts contain DieSlot but StationProfile->ExtensionDieSlotProfile is null"));
		}
		if (bAnyOutputTray && !StationProfile->ExtensionOutputSlotProfile)
		{
			Err(TEXT("ExtensionPorts contain OutputTray but StationProfile->ExtensionOutputSlotProfile is null"));
		}
		if (bAnyToolRack && !StationProfile->ExtensionToolSlotProfile)
		{
			Err(TEXT("ExtensionPorts contain ToolRack but StationProfile->ExtensionToolSlotProfile is null"));
		}
	}

	return Result;
}
#endif // WITH_EDITOR
