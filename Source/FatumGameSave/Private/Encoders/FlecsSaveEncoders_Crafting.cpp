// FlecsSaveEncoders_Crafting — implementations + static-init registry hooks.
//
// CRITICAL (v2 §M6): FCraftingStationInstance::MatchedRecipe is NEVER serialized.
// The decoder leaves it nullptr; RecipeMatchSystem re-derives it on the first sim
// tick post-load from current slot contents + active fuel. LastMatchedDigest is
// also reset to 0 so RecipeMatchSystem treats post-load state as definitively
// dirty (regardless of SlotDirtyDigest's saved value).
//
// CRITICAL (v2 §M11 + Phase 4): FSmelterInstance::ConsumedLedger entries hold a
// TObjectPtr<UFlecsEntityDefinition>. The remap table is entity_t-only, so recipe
// Definition asset references go through FlecsSaveRemap::GPathTable->RegisterPath
// on encode + ResolveDefinition on decode. GPathTable is set by the writer / reader
// (see FlecsSaveSnapshotWriter.cpp / FlecsSaveSnapshotReader.cpp). Missing assets
// drop the ledger row with a Warning — Smelter will re-evaluate at next Start.

#include "Encoders/FlecsSaveEncoders_Crafting.h"

#include "FlecsSaveAssetPathTable.h"
#include "FlecsSaveComponentRegistry.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveRemap.h"
#include "FlecsSaveTypeIds.h"

#include "FlecsCraftingComponents.h"
#include "FlecsCraftingTypes.h"
#include "FlecsEntityDefinition.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "Encoders/FlecsSaveEncoderHelpers.h"   // SaveValue template — moved out of anonymous ns to fix unity-build collision

#include "flecs.h"

using FlecsSaveEnc::SaveValue;

// ═══════════════════════════════════════════════════════════════
// FCraftingStationInstance  (TypeId 0x0600, Version 1)
//
// FIELDS PERSISTED:
//   - SlotDirtyDigest (uint64) — preserved so any post-load mutation can recompute
//     reliably even though the *value* is moot until first RecipeMatchSystem tick.
//   - FuelChargeSecondsRemaining (float) — designer-visible state survives load.
//   - ActiveFuelType (uint8 EFuelType) — paired with the reservoir.
//
// FIELDS RESET ON LOAD (NOT serialized):
//   - MatchedRecipe       = nullptr           (per v2 §M6; re-derived next tick)
//   - LastMatchedDigest   = 0                 (forces re-match)
//   - LastDiagnostic      = None              (any stale message would mislead UI)
//   - bSnapshotDirty      = true              (force UI re-publish on first flush)
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Crafting::Encode_CraftingStationInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FCraftingStationInstance* C = E.try_get<FCraftingStationInstance>();
	checkf(C, TEXT("Encode_CraftingStationInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<uint64>(Ar, C->SlotDirtyDigest);
	SaveValue<float>(Ar, C->FuelChargeSecondsRemaining);
	SaveValue<uint8>(Ar, static_cast<uint8>(C->ActiveFuelType));
	// MatchedRecipe, LastMatchedDigest, LastDiagnostic — NOT serialized (transient cache).
}

bool FlecsSaveEncoders_Crafting::Decode_CraftingStationInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_CraftingStationInstance: unknown version %u"), Version);
		return false;
	}

	FCraftingStationInstance C;
	uint8 ActiveFuelTypeByte = 0;
	Ar << C.SlotDirtyDigest;
	Ar << C.FuelChargeSecondsRemaining;
	Ar << ActiveFuelTypeByte;
	C.ActiveFuelType = static_cast<EFuelType>(ActiveFuelTypeByte);

	// Transient cache fields — per v2 §M6.
	C.MatchedRecipe     = nullptr;
	C.LastMatchedDigest = 0;
	C.LastDiagnostic    = ECraftingMatchDiagnostic::None;
	C.bSnapshotDirty    = true;   // force UI re-publish on first post-load flush

	E.set<FCraftingStationInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FCraftingSlots  (TypeId 0x0601, Version 1)
// Fixed-capacity table: kMaxCraftingSlots × int64 SlotEntityIds (REMAP) +
// ESlotRole::MAX × uint8 SlotRoleCounts. Capacity is intrinsic to the version.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Crafting::Encode_CraftingSlots(const flecs::entity& E, TArray<uint8>& Out)
{
	const FCraftingSlots* C = E.try_get<FCraftingSlots>();
	checkf(C, TEXT("Encode_CraftingSlots: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version

	// SlotEntityIds — REMAP each. Zero stays zero (unused slot beyond layout).
	for (int32 i = 0; i < kMaxCraftingSlots; ++i)
	{
		const uint32 SlotIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(C->SlotEntityIds[i]));
		SaveValue<uint32>(Ar, SlotIdx);
	}

	// SlotRoleCounts — plain uint8 array.
	for (int32 i = 0; i < static_cast<int32>(ESlotRole::MAX); ++i)
	{
		SaveValue<uint8>(Ar, C->SlotRoleCounts[i]);
	}
}

bool FlecsSaveEncoders_Crafting::Decode_CraftingSlots(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_CraftingSlots: unknown version %u"), Version);
		return false;
	}

	FCraftingSlots C;
	for (int32 i = 0; i < kMaxCraftingSlots; ++i)
	{
		uint32 SlotIdx = 0xFFFFFFFFu;
		Ar << SlotIdx;
		C.SlotEntityIds[i] = static_cast<int64>(FlecsSaveRemap::ResolveSaveIndex(SlotIdx));
		// 0xFFFFFFFF → 0 sentinel (correct for unused slot).
	}
	for (int32 i = 0; i < static_cast<int32>(ESlotRole::MAX); ++i)
	{
		uint8 Count = 0;
		Ar << Count;
		C.SlotRoleCounts[i] = Count;
	}

	E.set<FCraftingSlots>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FFuelSlot  (TypeId 0x0602, Version 1)
// Single int64 REMAP. 0 sentinel = station has no fuel slot.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Crafting::Encode_FuelSlot(const flecs::entity& E, TArray<uint8>& Out)
{
	const FFuelSlot* C = E.try_get<FFuelSlot>();
	checkf(C, TEXT("Encode_FuelSlot: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version

	const uint32 FuelIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(C->FuelSlotEntityId));
	SaveValue<uint32>(Ar, FuelIdx);
}

bool FlecsSaveEncoders_Crafting::Decode_FuelSlot(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_FuelSlot: unknown version %u"), Version);
		return false;
	}

	uint32 FuelIdx = 0xFFFFFFFFu;
	Ar << FuelIdx;

	FFuelSlot C;
	C.FuelSlotEntityId = static_cast<int64>(FlecsSaveRemap::ResolveSaveIndex(FuelIdx));

	E.set<FFuelSlot>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FCraftingSlotBackRef  (TypeId 0x0603, Version 1)
// Lives on slot CONTAINER entities. StationEntityId REMAP + 3 intrinsic bytes.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Crafting::Encode_CraftingSlotBackRef(const flecs::entity& E, TArray<uint8>& Out)
{
	const FCraftingSlotBackRef* C = E.try_get<FCraftingSlotBackRef>();
	checkf(C, TEXT("Encode_CraftingSlotBackRef: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version

	const uint32 StationIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(C->StationEntityId));
	SaveValue<uint32>(Ar, StationIdx);
	SaveValue<uint16>(Ar, C->SlotIndex);
	SaveValue<uint8>(Ar, C->Role);
	SaveValue<uint8>(Ar, C->OwningPortIndex);
}

bool FlecsSaveEncoders_Crafting::Decode_CraftingSlotBackRef(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_CraftingSlotBackRef: unknown version %u"), Version);
		return false;
	}

	uint32 StationIdx = 0xFFFFFFFFu;
	uint16 SlotIndex = 0;
	uint8 Role = 0;
	uint8 OwningPortIndex = 0xFF;
	Ar << StationIdx;
	Ar << SlotIndex;
	Ar << Role;
	Ar << OwningPortIndex;

	FCraftingSlotBackRef C;
	C.StationEntityId  = static_cast<int64>(FlecsSaveRemap::ResolveSaveIndex(StationIdx));
	C.SlotIndex        = SlotIndex;
	C.Role             = Role;
	C.OwningPortIndex  = OwningPortIndex;

	E.set<FCraftingSlotBackRef>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FSmelterInstance  (TypeId 0x0604, Version 1)
//
// Persistent: Phase, ProgressSeconds, DurationCached, bStart/bCancel flags,
//             ConsumedLedger[] (Definition path index + Count + SourceSlotIndex).
//
// ConsumedLedger entries reference UFlecsEntityDefinition* via the active
// FlecsSaveRemap::GPathTable. A missing asset on load drops the row with a
// Warning (Smelter will refund only the surviving rows; remaining ingredients
// are forfeit — acceptable per Phase 4 fault-tolerance).
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Crafting::Encode_SmelterInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FSmelterInstance* C = E.try_get<FSmelterInstance>();
	checkf(C, TEXT("Encode_SmelterInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FFlecsSaveAssetPathTable* PathTable = FlecsSaveRemap::GPathTable;
	checkf(PathTable, TEXT("Encode_SmelterInstance: GPathTable is null — writer didn't set TLS"));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<uint8>(Ar, static_cast<uint8>(C->Phase));
	SaveValue<float>(Ar, C->ProgressSeconds);
	SaveValue<float>(Ar, C->DurationCached);
	SaveValue<uint8>(Ar, C->bStartRequested  ? 1u : 0u);
	SaveValue<uint8>(Ar, C->bCancelRequested ? 1u : 0u);

	const int32 LedgerCount = C->ConsumedLedger.Num();
	checkf(LedgerCount >= 0 && LedgerCount <= 256,
		TEXT("Encode_SmelterInstance: entity %llu has LedgerCount=%d outside [0, 256]"),
		static_cast<uint64>(E.id()), LedgerCount);
	SaveValue<int32>(Ar, LedgerCount);

	for (const FConsumedIngredient& Row : C->ConsumedLedger)
	{
		// Definition pointer → path-table index. Null definition writes kInvalidIndex
		// (defensive; production ledgers should always carry a resolved definition,
		// but a stray null shouldn't crash the save).
		const uint32 DefIdx = Row.Definition
			? PathTable->RegisterPath(Row.Definition.Get())
			: FFlecsSaveAssetPathTable::kInvalidIndex;
		SaveValue<uint32>(Ar, DefIdx);
		SaveValue<int32>(Ar, Row.Count);
		SaveValue<uint8>(Ar, Row.SourceSlotIndex);
	}
}

bool FlecsSaveEncoders_Crafting::Decode_SmelterInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FFlecsSaveAssetPathTable* PathTable = FlecsSaveRemap::GPathTable;
	checkf(PathTable, TEXT("Decode_SmelterInstance: GPathTable is null — reader didn't set TLS"));

	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_SmelterInstance: unknown version %u"), Version);
		return false;
	}

	FSmelterInstance C;
	uint8 PhaseByte = 0;
	uint8 bStartByte = 0;
	uint8 bCancelByte = 0;
	Ar << PhaseByte;
	Ar << C.ProgressSeconds;
	Ar << C.DurationCached;
	Ar << bStartByte;
	Ar << bCancelByte;
	C.Phase            = static_cast<EProcessPhase>(PhaseByte);
	C.bStartRequested  = (bStartByte  != 0);
	C.bCancelRequested = (bCancelByte != 0);

	int32 LedgerCount = 0;
	Ar << LedgerCount;
	if (LedgerCount < 0 || LedgerCount > 256)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("Decode_SmelterInstance: corrupt LedgerCount=%d (max 256)"), LedgerCount);
		return false;
	}

	C.ConsumedLedger.Reserve(LedgerCount);
	for (int32 i = 0; i < LedgerCount; ++i)
	{
		uint32 DefIdx = FFlecsSaveAssetPathTable::kInvalidIndex;
		int32 Count = 0;
		uint8 SourceSlotIndex = 0;
		Ar << DefIdx;
		Ar << Count;
		Ar << SourceSlotIndex;

		FConsumedIngredient Row;
		Row.Count = Count;
		Row.SourceSlotIndex = SourceSlotIndex;
		if (DefIdx != FFlecsSaveAssetPathTable::kInvalidIndex)
		{
			Row.Definition = PathTable->ResolveDefinition(DefIdx);
			if (!Row.Definition)
			{
				UE_LOG(LogFlecsSave, Warning,
					TEXT("Decode_SmelterInstance: entity %llu ledger row %d failed asset resolve (index %u) — dropping row"),
					static_cast<uint64>(E.id()), i, DefIdx);
				continue;
			}
		}
		else
		{
			UE_LOG(LogFlecsSave, Warning,
				TEXT("Decode_SmelterInstance: entity %llu ledger row %d has kInvalidIndex — dropping row"),
				static_cast<uint64>(E.id()), i);
			continue;
		}
		C.ConsumedLedger.Add(MoveTemp(Row));
	}

	E.set<FSmelterInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FCraftingSlotLockedByStation  (TypeId 0x0605, Version 1)
// Lives on slot CONTAINER entities while owning station is Processing/Stalled.
// OwningStationEntityId is REMAP. Persisted so post-load slot stays locked
// until SmelterProcessSystem re-evaluates and either renews or releases.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Crafting::Encode_CraftingSlotLockedByStation(const flecs::entity& E, TArray<uint8>& Out)
{
	const FCraftingSlotLockedByStation* C = E.try_get<FCraftingSlotLockedByStation>();
	checkf(C, TEXT("Encode_CraftingSlotLockedByStation: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version

	const uint32 OwningIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(C->OwningStationEntityId));
	SaveValue<uint32>(Ar, OwningIdx);
}

bool FlecsSaveEncoders_Crafting::Decode_CraftingSlotLockedByStation(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_CraftingSlotLockedByStation: unknown version %u"), Version);
		return false;
	}

	uint32 OwningIdx = 0xFFFFFFFFu;
	Ar << OwningIdx;

	FCraftingSlotLockedByStation C;
	C.OwningStationEntityId = static_cast<int64>(FlecsSaveRemap::ResolveSaveIndex(OwningIdx));

	E.set<FCraftingSlotLockedByStation>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FStationEffectiveLayout  (TypeId 0x0606, Version 1)
// All fields intrinsic (no cross-entity refs). Recomputable, but persisting
// saves a tick of RecomputeEffectiveLayout work + avoids a one-frame UI flash.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Crafting::Encode_StationEffectiveLayout(const flecs::entity& E, TArray<uint8>& Out)
{
	const FStationEffectiveLayout* C = E.try_get<FStationEffectiveLayout>();
	checkf(C, TEXT("Encode_StationEffectiveLayout: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<uint8>(Ar, C->EffectiveSlotCount);
	SaveValue<float>(Ar, C->EffectiveFuelCeiling);
	SaveValue<uint8>(Ar, C->ExtensionPortsOccupiedCount);
	SaveValue<uint16>(Ar, C->MissingRequiredRolesBitmask);
}

bool FlecsSaveEncoders_Crafting::Decode_StationEffectiveLayout(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_StationEffectiveLayout: unknown version %u"), Version);
		return false;
	}

	FStationEffectiveLayout C;
	Ar << C.EffectiveSlotCount;
	Ar << C.EffectiveFuelCeiling;
	Ar << C.ExtensionPortsOccupiedCount;
	Ar << C.MissingRequiredRolesBitmask;

	E.set<FStationEffectiveLayout>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// AUTO-REGISTRATION
// ═══════════════════════════════════════════════════════════════

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_CraftingStationInstance, FCraftingStationInstance, 1,
	FlecsSaveEncoders_Crafting::Encode_CraftingStationInstance,
	FlecsSaveEncoders_Crafting::Decode_CraftingStationInstance)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_CraftingSlots, FCraftingSlots, 1,
	FlecsSaveEncoders_Crafting::Encode_CraftingSlots,
	FlecsSaveEncoders_Crafting::Decode_CraftingSlots)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_FuelSlot, FFuelSlot, 1,
	FlecsSaveEncoders_Crafting::Encode_FuelSlot,
	FlecsSaveEncoders_Crafting::Decode_FuelSlot)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_CraftingSlotBackRef, FCraftingSlotBackRef, 1,
	FlecsSaveEncoders_Crafting::Encode_CraftingSlotBackRef,
	FlecsSaveEncoders_Crafting::Decode_CraftingSlotBackRef)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_SmelterInstance, FSmelterInstance, 1,
	FlecsSaveEncoders_Crafting::Encode_SmelterInstance,
	FlecsSaveEncoders_Crafting::Decode_SmelterInstance)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_CraftingSlotLockedByStation, FCraftingSlotLockedByStation, 1,
	FlecsSaveEncoders_Crafting::Encode_CraftingSlotLockedByStation,
	FlecsSaveEncoders_Crafting::Decode_CraftingSlotLockedByStation)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_StationEffectiveLayout, FStationEffectiveLayout, 1,
	FlecsSaveEncoders_Crafting::Encode_StationEffectiveLayout,
	FlecsSaveEncoders_Crafting::Decode_StationEffectiveLayout)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagCraftingStation, FTagCraftingStation)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagCraftingFuel, FTagCraftingFuel)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagStationDisabled, FTagStationDisabled)
