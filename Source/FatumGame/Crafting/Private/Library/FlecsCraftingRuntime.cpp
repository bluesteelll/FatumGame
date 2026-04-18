// FlecsCraftingRuntime — sim-thread crafting helpers.

#include "FlecsCraftingRuntime.h"
#include "FlecsCraftingComponents.h"
#include "FlecsCraftingRecipeRegistry.h"
#include "FlecsCraftingRecipeDef.h"
#include "FlecsCraftingStationProfile.h"
#include "FlecsCraftingLog.h"
#include "FlecsItemComponents.h"
#include "FlecsEntityComponents.h"
#include "FlecsEntityDefinition.h"
#include "FlecsItemDefinition.h"
#include "flecs.h"

namespace FlecsCraftingRuntime
{

// ═══════════════════════════════════════════════════════════════
// DIRTY FLAG
// ═══════════════════════════════════════════════════════════════

void InvalidateMatchCache(flecs::entity StationE)
{
	if (!StationE.is_valid() || !StationE.is_alive()) return;
	if (StationE.has<FTagCraftingStationDestroying>()) return;

	FCraftingStationInstance* Inst = StationE.try_get_mut<FCraftingStationInstance>();
	if (!Inst) return;
	Inst->bSnapshotDirty = true;
}

void MarkStationDirtyByContainer(flecs::world& World, int64 OldContainerId, int64 NewContainerId)
{
	auto ResolveAndDirty = [&World](int64 ContainerId)
	{
		if (ContainerId == 0) return;
		flecs::entity ContainerE = World.entity(static_cast<flecs::entity_t>(ContainerId));
		if (!ContainerE.is_valid() || !ContainerE.is_alive()) return;

		const FCraftingSlotBackRef* Back = ContainerE.try_get<FCraftingSlotBackRef>();
		if (!Back || Back->StationEntityId == 0) return;  // Container is not a crafting slot.

		flecs::entity StationE = World.entity(static_cast<flecs::entity_t>(Back->StationEntityId));
		InvalidateMatchCache(StationE);
	};

	ResolveAndDirty(OldContainerId);
	if (NewContainerId != OldContainerId)
	{
		ResolveAndDirty(NewContainerId);
	}
}

// ═══════════════════════════════════════════════════════════════
// DIGEST + MATCH
// ═══════════════════════════════════════════════════════════════

namespace
{
	/**
	 * Collect (ItemTypeId, Count) pairs across all slots whose role matches the filter mask.
	 * RoleMask bits are (1u << ESlotRole value). Sorted ascending by TypeId to stabilise hash.
	 */
	void CollectSlotContents(
		flecs::entity StationE,
		uint32 RoleMask,
		TArray<TPair<int32, int32>>& OutPairs)
	{
		OutPairs.Reset();
		const FCraftingSlots* Slots = StationE.try_get<FCraftingSlots>();
		if (!Slots) return;
		const FCraftingStationStatic* Static = StationE.try_get<FCraftingStationStatic>();
		if (!Static || !Static->Profile) return;

		flecs::world World = StationE.world();

		const TArray<FSlotLayoutDef>& Layout = Static->Profile->SlotLayout;
		const int32 SlotCount = FMath::Min(Layout.Num(), kMaxCraftingSlots);

		for (int32 i = 0; i < SlotCount; ++i)
		{
			const uint32 RoleBit = (1u << static_cast<uint32>(Layout[i].Role));
			if ((RoleMask & RoleBit) == 0) continue;

			const int64 SlotId = Slots->SlotEntityIds[i];
			if (SlotId == 0) continue;

			World.each([SlotId, &OutPairs](flecs::entity ItemE, const FContainedIn& CI, const FItemInstance& Inst)
			{
				if (CI.ContainerEntityId != SlotId) return;
				const FItemStaticData* ItemStatic = ItemE.try_get<FItemStaticData>();
				if (!ItemStatic) return;

				// Accumulate into existing entry for the TypeId when possible — ring-buffer style.
				for (TPair<int32, int32>& Pair : OutPairs)
				{
					if (Pair.Key == ItemStatic->TypeId)
					{
						Pair.Value += Inst.Count;
						return;
					}
				}
				OutPairs.Emplace(ItemStatic->TypeId, Inst.Count);
			});
		}

		OutPairs.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
		{
			return A.Key < B.Key;
		});
	}

	/** Read first item in fuel slot and return its FCraftingFuelItemData (or nullptr if empty). */
	const FCraftingFuelItemData* PeekFuelSlotFirstItem(flecs::entity StationE, flecs::entity& OutFirstItem)
	{
		OutFirstItem = flecs::entity();

		const FFuelSlot* FuelSlot = StationE.try_get<FFuelSlot>();
		if (!FuelSlot || FuelSlot->FuelSlotEntityId == 0) return nullptr;

		flecs::world World = StationE.world();
		const FCraftingFuelItemData* FoundData = nullptr;
		flecs::entity FoundItem;

		World.each([FuelSlot, &FoundData, &FoundItem](flecs::entity ItemE, const FContainedIn& CI)
		{
			if (FoundData) return;  // Keep first match.
			if (CI.ContainerEntityId != FuelSlot->FuelSlotEntityId) return;

			const FCraftingFuelItemData* D = ItemE.try_get<FCraftingFuelItemData>();
			if (!D) return;
			FoundData = D;
			FoundItem = ItemE;
		});

		OutFirstItem = FoundItem;
		return FoundData;
	}
}

uint64 ComputeSlotDigest(flecs::entity StationE)
{
	if (!StationE.is_valid() || !StationE.is_alive()) return 0;

	const uint32 Mask = (1u << static_cast<uint32>(ESlotRole::MaterialInput))
	                  | (1u << static_cast<uint32>(ESlotRole::Fuel));

	TArray<TPair<int32, int32>> Pairs;
	CollectSlotContents(StationE, Mask, Pairs);

	uint32 Seed = 0;
	for (const TPair<int32, int32>& Pair : Pairs)
	{
		Seed = HashCombine(Seed, GetTypeHash(Pair.Key));
		Seed = HashCombine(Seed, GetTypeHash(Pair.Value));
	}

	// Fold in active fuel + reservoir bucket (coarse — we don't want tiny float deltas to churn).
	if (const FCraftingStationInstance* Inst = StationE.try_get<FCraftingStationInstance>())
	{
		Seed = HashCombine(Seed, GetTypeHash(static_cast<uint8>(Inst->ActiveFuelType)));
		const uint32 FuelBucket = static_cast<uint32>(Inst->FuelChargeSecondsRemaining);
		Seed = HashCombine(Seed, GetTypeHash(FuelBucket));
	}

	return static_cast<uint64>(Seed) | (static_cast<uint64>(Pairs.Num()) << 32);
}

const UFlecsCraftingRecipeDef* MatchRecipe(flecs::entity StationE, UFlecsCraftingRecipeRegistry* Registry)
{
	if (!StationE.is_valid() || !StationE.is_alive()) return nullptr;
	if (StationE.has<FTagCraftingStationDestroying>()) return nullptr;

	FCraftingStationInstance* Inst = StationE.try_get_mut<FCraftingStationInstance>();
	if (!Inst) return nullptr;

	// MN5 — reset unconditionally to None so stale diagnostics don't carry when match succeeds.
	Inst->LastDiagnostic = ECraftingMatchDiagnostic::None;

	const FCraftingStationStatic* Static = StationE.try_get<FCraftingStationStatic>();
	if (!Static)
	{
		Inst->MatchedRecipe = nullptr;
		Inst->LastDiagnostic = ECraftingMatchDiagnostic::NoMatch;
		return nullptr;
	}

	if (!Registry)
	{
		Inst->MatchedRecipe = nullptr;
		Inst->LastDiagnostic = ECraftingMatchDiagnostic::NoMatch;
		return nullptr;
	}

	// Gather MaterialInput contents.
	TArray<TPair<int32, int32>> Inputs;
	const uint32 InputMask = (1u << static_cast<uint32>(ESlotRole::MaterialInput));
	CollectSlotContents(StationE, InputMask, Inputs);

	// Determine active fuel classification.
	const EFuelType ActiveFuel = ResolveFuelType(StationE);
	const float FuelCharge = Inst->FuelChargeSecondsRemaining;

	const UFlecsCraftingRecipeDef* Match = Registry->FindMatch(
		Static->StationType, ActiveFuel, Inputs, FuelCharge);

	Inst->MatchedRecipe = Match;
	if (!Match)
	{
		Inst->LastDiagnostic = ECraftingMatchDiagnostic::NoMatch;
	}
	return Match;
}

// ═══════════════════════════════════════════════════════════════
// FUEL
// ═══════════════════════════════════════════════════════════════

EFuelType ResolveFuelType(flecs::entity StationE)
{
	if (!StationE.is_valid() || !StationE.is_alive()) return EFuelType::None;

	const FCraftingStationInstance* Inst = StationE.try_get<FCraftingStationInstance>();
	if (Inst && Inst->FuelChargeSecondsRemaining > 0.f && Inst->ActiveFuelType != EFuelType::None)
	{
		return Inst->ActiveFuelType;
	}

	flecs::entity FirstItem;
	if (const FCraftingFuelItemData* Data = PeekFuelSlotFirstItem(StationE, FirstItem))
	{
		return Data->FuelType;
	}
	return EFuelType::None;
}

bool TryConsumeFuelSlot(flecs::entity StationE, float /*AmountSeconds*/)
{
	if (!StationE.is_valid() || !StationE.is_alive()) return false;
	if (StationE.has<FTagCraftingStationDestroying>()) return false;

	FCraftingStationInstance* Inst = StationE.try_get_mut<FCraftingStationInstance>();
	if (!Inst) return false;

	const FCraftingStationStatic* Static = StationE.try_get<FCraftingStationStatic>();
	if (!Static) return false;

	flecs::entity FirstItem;
	const FCraftingFuelItemData* Data = PeekFuelSlotFirstItem(StationE, FirstItem);

	// Case A — reservoir already primed, no item visible. ActiveFuelType stays (grace burn).
	if (!Data)
	{
		// When reservoir drained and slot empty, clear ActiveFuelType.
		if (Inst->FuelChargeSecondsRemaining <= 0.f && Inst->ActiveFuelType != EFuelType::None)
		{
			Inst->ActiveFuelType = EFuelType::None;
			return true;
		}
		return false;
	}

	// Station supports this fuel?
	const uint16 FuelBit = static_cast<uint16>(Data->FuelType);
	if ((Static->SupportedFuelMask & FuelBit) == 0)
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("Station '%s' rejects fuel type %u (not in SupportedFuelMask 0x%04x)"),
			*Static->StationName.ToString(), static_cast<uint32>(Data->FuelType),
			static_cast<uint32>(Static->SupportedFuelMask));
		return false;
	}

	// Case B — reservoir empty, no active type: adopt this item's type and consume one unit.
	if (Inst->FuelChargeSecondsRemaining <= 0.f && Inst->ActiveFuelType == EFuelType::None)
	{
		Inst->ActiveFuelType = Data->FuelType;
		Inst->FuelChargeSecondsRemaining = Data->ChargeSecondsPerUnit;
		checkf(Inst->FuelChargeSecondsRemaining > 0.f || Inst->ActiveFuelType == EFuelType::None,
			TEXT("Invariant: FuelChargeSecondsRemaining > 0 implies ActiveFuelType != None"));

		// Consume one unit.
		if (FirstItem.is_valid() && FirstItem.is_alive())
		{
			FItemInstance* ItemInst = FirstItem.try_get_mut<FItemInstance>();
			if (ItemInst)
			{
				--ItemInst->Count;
				if (ItemInst->Count <= 0) FirstItem.destruct();
			}
		}
		return true;
	}

	// Case C — reservoir has charge and item matches active type: top up.
	if (Inst->ActiveFuelType == Data->FuelType)
	{
		Inst->FuelChargeSecondsRemaining += Data->ChargeSecondsPerUnit;
		if (FirstItem.is_valid() && FirstItem.is_alive())
		{
			FItemInstance* ItemInst = FirstItem.try_get_mut<FItemInstance>();
			if (ItemInst)
			{
				--ItemInst->Count;
				if (ItemInst->Count <= 0) FirstItem.destruct();
			}
		}
		return true;
	}

	// Case D — type mismatch while reservoir active: REJECT (item stays, no state change).
	UE_LOG(LogCrafting, Warning,
		TEXT("Station '%s' refusing fuel type %u while active type %u reservoir %.1fs burns"),
		*Static->StationName.ToString(),
		static_cast<uint32>(Data->FuelType),
		static_cast<uint32>(Inst->ActiveFuelType),
		Inst->FuelChargeSecondsRemaining);
	return false;
}

// ═══════════════════════════════════════════════════════════════
// SNAPSHOT
// ═══════════════════════════════════════════════════════════════

void BuildSnapshot(flecs::entity StationE, FCraftingStationSnapshot& OutSnapshot)
{
	OutSnapshot.StationName = NAME_None;
	OutSnapshot.DebugDisplayName = FText::GetEmpty();
	OutSnapshot.MatchedRecipeName = NAME_None;
	OutSnapshot.Diagnostic = 0;
	OutSnapshot.ActiveFuelType = 0;
	OutSnapshot.FuelChargeSecondsRemaining = 0.f;
	OutSnapshot.Slots.Reset();

	if (!StationE.is_valid() || !StationE.is_alive()) return;

	const FCraftingStationStatic* Static = StationE.try_get<FCraftingStationStatic>();
	const FCraftingStationInstance* Inst = StationE.try_get<FCraftingStationInstance>();
	const FCraftingSlots* Slots = StationE.try_get<FCraftingSlots>();

	if (!Static || !Inst || !Slots || !Static->Profile) return;

	OutSnapshot.StationName      = Static->StationName;
	OutSnapshot.DebugDisplayName = Static->Profile->DisplayName;
	OutSnapshot.Diagnostic       = static_cast<uint8>(Inst->LastDiagnostic);
	OutSnapshot.ActiveFuelType   = static_cast<uint8>(Inst->ActiveFuelType);
	OutSnapshot.FuelChargeSecondsRemaining = Inst->FuelChargeSecondsRemaining;

	if (Inst->MatchedRecipe)
	{
		OutSnapshot.MatchedRecipeName = Inst->MatchedRecipe->GetFName();
	}

	flecs::world World = StationE.world();
	const TArray<FSlotLayoutDef>& Layout = Static->Profile->SlotLayout;
	const int32 SlotCount = FMath::Min(Layout.Num(), kMaxCraftingSlots);

	OutSnapshot.Slots.Reserve(SlotCount);
	for (int32 i = 0; i < SlotCount; ++i)
	{
		FCraftingStationSlotSnapshot Row;
		Row.SlotName  = Layout[i].SlotName;
		Row.Role      = static_cast<uint8>(Layout[i].Role);
		Row.ItemCount = 0;

		const int64 SlotId = Slots->SlotEntityIds[i];
		if (SlotId != 0)
		{
			World.each([SlotId, &Row](flecs::entity ItemE, const FContainedIn& CI, const FItemInstance& ItemInst)
			{
				if (CI.ContainerEntityId != SlotId) return;
				++Row.ItemCount;

				if (Row.ItemLines.Num() >= Row.ItemLines.GetSlack() + 8) return;  // Inline cap guard.

				const FItemStaticData* Data = ItemE.try_get<FItemStaticData>();
				const FText ItemName = Data
					? FText::FromName(Data->ItemName)
					: FText::FromString(TEXT("Unknown"));
				Row.ItemLines.Add(FText::Format(
					FText::FromString(TEXT("{0}x {1}")),
					FText::AsNumber(ItemInst.Count),
					ItemName));
			});
		}

		OutSnapshot.Slots.Add(MoveTemp(Row));
	}
}

} // namespace FlecsCraftingRuntime
