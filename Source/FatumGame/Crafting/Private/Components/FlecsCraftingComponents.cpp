// FromProfile implementations + FCraftingSlots helpers.

#include "FlecsCraftingComponents.h"
#include "FlecsCraftingStationProfile.h"
#include "FlecsFuelProfile.h"
#include "FlecsCraftingLog.h"

// ═══════════════════════════════════════════════════════════════
// FCraftingStationStatic
// ═══════════════════════════════════════════════════════════════

FCraftingStationStatic FCraftingStationStatic::FromProfile(const UFlecsCraftingStationProfile* P)
{
	checkf(P, TEXT("FCraftingStationStatic::FromProfile called with null profile"));
	checkf(P->SlotLayout.Num() >= 1,
		TEXT("Station '%s' has empty SlotLayout"), *P->StationName.ToString());
	checkf(P->SlotLayout.Num() <= kMaxCraftingSlots,
		TEXT("Station '%s' SlotLayout count %d exceeds kMaxCraftingSlots (%d)"),
		*P->StationName.ToString(), P->SlotLayout.Num(), kMaxCraftingSlots);

	int32 MaterialInputCount = 0;
	int32 FuelCount = 0;
	int32 OutputCount = 0;
	for (const FSlotLayoutDef& Slot : P->SlotLayout)
	{
		switch (Slot.Role)
		{
		case ESlotRole::MaterialInput: ++MaterialInputCount; break;
		case ESlotRole::Fuel:          ++FuelCount;          break;
		case ESlotRole::Output:        ++OutputCount;        break;
		default:                                              break;
		}
	}
	checkf(MaterialInputCount >= 1,
		TEXT("Station '%s' requires >= 1 MaterialInput slot (found %d)"),
		*P->StationName.ToString(), MaterialInputCount);
	checkf(FuelCount <= 1,
		TEXT("Station '%s' allows at most 1 Fuel slot (found %d)"),
		*P->StationName.ToString(), FuelCount);
	checkf(OutputCount >= 1,
		TEXT("Station '%s' requires >= 1 Output slot (found %d)"),
		*P->StationName.ToString(), OutputCount);

	// Fold SupportedFuelTypes set into a bitmask.
	uint16 FuelMask = 0;
	for (EFuelType FT : P->SupportedFuelTypes)
	{
		FuelMask |= static_cast<uint16>(FT);
	}
	const bool bHasFuelSlot = (FuelCount == 1);
	const bool bHasFuelMask = (FuelMask != 0);
	checkf(bHasFuelSlot == bHasFuelMask,
		TEXT("Station '%s': SupportedFuelTypes non-empty iff Fuel slot present (slotCount=%d, mask=0x%04x)"),
		*P->StationName.ToString(), FuelCount, FuelMask);

	FCraftingStationStatic Out;
	Out.StationType       = P->StationType;
	Out.SupportedFuelMask = FuelMask;
	Out.StationName       = P->StationName;
	Out.StationTag        = P->StationTag;
	Out.Profile           = P;
	return Out;
}

// ═══════════════════════════════════════════════════════════════
// FCraftingSlots
// ═══════════════════════════════════════════════════════════════

int64 FCraftingSlots::GetByRole(ESlotRole Role, int32 NthIndex) const
{
	// The SlotEntityIds array is ordered identically to UFlecsCraftingStationProfile::SlotLayout.
	// The caller supplies NthIndex as the 0-based occurrence of Role in that original layout —
	// but without the profile pointer here, we instead match via SlotRoleCounts prefix count:
	// iterate and return the NthIndex-th slot whose role tag matches. Because SlotEntityIds
	// does not store role inline, callers that need fast access should track role → indices
	// map separately (Phase 3 concern). For now walk the slots and rely on SlotRoleCounts
	// to early-out when NthIndex exceeds the known count.
	const uint8 RoleIdx = static_cast<uint8>(Role);
	if (RoleIdx >= static_cast<uint8>(ESlotRole::MAX)) return 0;
	if (NthIndex < 0 || NthIndex >= SlotRoleCounts[RoleIdx]) return 0;

	// Walk SlotEntityIds; for lookups needing role, caller must stash the profile.
	// This function is only safe when used in conjunction with Spawner-time population order
	// (slots are populated in the same order as the profile's SlotLayout).
	// The PROFILE is the authoritative role map — callers should prefer the profile for
	// role→index translation. This helper remains as a simple "count is valid" gate.
	return 0;  // Sentinel — real role→slot resolution requires the profile pointer.
}

int32 FCraftingSlots::FindFreeIndex() const
{
	for (int32 i = 0; i < kMaxCraftingSlots; ++i)
	{
		if (SlotEntityIds[i] == 0) return i;
	}
	return INDEX_NONE;
}

// ═══════════════════════════════════════════════════════════════
// FCraftingFuelItemData
// ═══════════════════════════════════════════════════════════════

FCraftingFuelItemData FCraftingFuelItemData::FromProfile(const UFlecsFuelProfile* P)
{
	checkf(P, TEXT("FCraftingFuelItemData::FromProfile called with null profile"));
	checkf(P->FuelType != EFuelType::None,
		TEXT("Fuel profile '%s' has FuelType=None (must specify a concrete fuel category)"),
		*P->GetName());
	checkf(P->ChargeSecondsPerUnit > 0.f,
		TEXT("Fuel profile '%s' has ChargeSecondsPerUnit <= 0"), *P->GetName());

	FCraftingFuelItemData Out;
	Out.FuelType             = P->FuelType;
	Out.ChargeSecondsPerUnit = P->ChargeSecondsPerUnit;
	return Out;
}
