// FlecsSaveEncoders_Multiblock — implementations + static-init registry hooks.
//
// Cross-entity refs (ChildEntityIds, AnchorEntityId, PortOccupants[]) go through
// FlecsSaveRemap::EntityToSaveIndex / ResolveSaveIndex.

#include "Encoders/FlecsSaveEncoders_Multiblock.h"

#include "FlecsSaveComponentRegistry.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveRemap.h"
#include "FlecsSaveTypeIds.h"

#include "FlecsMultiblockComponents.h"  // FMultiblockChildren, FMultiblockChildOf, FMultiblockExtensions,
                                        // FMultiblockChildSlot (inline), all tags

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "Encoders/FlecsSaveEncoderHelpers.h"   // SaveValue template — moved out of anonymous ns to fix unity-build collision

#include "flecs.h"

using FlecsSaveEnc::SaveValue;

namespace
{
	// Fixed array lengths from the component definitions — pinned by sizeof asserts in
	// the headers. If a contributor changes ChildSlots[N] / PortOccupants[N] capacity
	// they must bump the encoder version.
	constexpr int32 kChildSlotsArrayLen = 15; // matches FMultiblockChildren::ChildSlots[15]
	constexpr int32 kPortOccupantsLen   = 8;  // matches FMultiblockExtensions::PortOccupants[8]
}

// ═══════════════════════════════════════════════════════════════
// FMultiblockChildren  (TypeId 0x0700, Version 1)
//
// Anchor-side roster of bonded child slots. Fixed-capacity ChildSlots[15], each
// row carries a REMAP ChildEntityId + cached metadata (PartRole, bSwappable,
// bIsExtension, PortIndex). ChildCount tracks live slot population.
//
// AnchorYawSnappedDeg (uint16) is stamped ONCE at bond time (one of {0,90,180,270}).
// Always serialized intact — detection / attach systems use it for rotated offsets.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Multiblock::Encode_MultiblockChildren(const flecs::entity& E, TArray<uint8>& Out)
{
	const FMultiblockChildren* C = E.try_get<FMultiblockChildren>();
	checkf(C, TEXT("Encode_MultiblockChildren: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<uint8>(Ar, C->ChildCount);
	SaveValue<uint16>(Ar, C->AnchorYawSnappedDeg);

	for (int32 i = 0; i < kChildSlotsArrayLen; ++i)
	{
		const FMultiblockChildSlot& Slot = C->ChildSlots[i];

		const uint32 ChildIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(Slot.ChildEntityId));
		SaveValue<uint32>(Ar, ChildIdx);

		FName PartRoleTmp = Slot.PartRole;
		Ar << PartRoleTmp;

		SaveValue<uint8>(Ar, Slot.bSwappable);
		SaveValue<uint8>(Ar, Slot.bIsExtension);
		SaveValue<uint8>(Ar, Slot.PortIndex);
	}
}

bool FlecsSaveEncoders_Multiblock::Decode_MultiblockChildren(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_MultiblockChildren: unknown version %u"), Version);
		return false;
	}

	FMultiblockChildren C;
	Ar << C.ChildCount;
	Ar << C.AnchorYawSnappedDeg;

	for (int32 i = 0; i < kChildSlotsArrayLen; ++i)
	{
		uint32 ChildIdx = 0xFFFFFFFFu;
		FName PartRole;
		uint8 bSwappable = 0;
		uint8 bIsExtension = 0;
		uint8 PortIndex = 0;
		Ar << ChildIdx;
		Ar << PartRole;
		Ar << bSwappable;
		Ar << bIsExtension;
		Ar << PortIndex;

		C.ChildSlots[i].ChildEntityId = static_cast<int64>(FlecsSaveRemap::ResolveSaveIndex(ChildIdx));
		C.ChildSlots[i].PartRole      = PartRole;
		C.ChildSlots[i].bSwappable    = bSwappable;
		C.ChildSlots[i].bIsExtension  = bIsExtension;
		C.ChildSlots[i].PortIndex     = PortIndex;
	}

	E.set<FMultiblockChildren>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FMultiblockChildOf  (TypeId 0x0701, Version 1)
// Child-side back ref. Single int64 REMAP.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Multiblock::Encode_MultiblockChildOf(const flecs::entity& E, TArray<uint8>& Out)
{
	const FMultiblockChildOf* C = E.try_get<FMultiblockChildOf>();
	checkf(C, TEXT("Encode_MultiblockChildOf: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version

	const uint32 AnchorIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(C->AnchorEntityId));
	SaveValue<uint32>(Ar, AnchorIdx);
}

bool FlecsSaveEncoders_Multiblock::Decode_MultiblockChildOf(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_MultiblockChildOf: unknown version %u"), Version);
		return false;
	}

	uint32 AnchorIdx = 0xFFFFFFFFu;
	Ar << AnchorIdx;

	FMultiblockChildOf C;
	C.AnchorEntityId = static_cast<int64>(FlecsSaveRemap::ResolveSaveIndex(AnchorIdx));

	E.set<FMultiblockChildOf>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FMultiblockExtensions  (TypeId 0x0702, Version 1)
// Per-station extension roster. PortOccupants[8] REMAP each, PortCount intrinsic.
// Lives only on stations whose blueprint has at least one ExtensionPort —
// SetupStationInstance handles initialization on respawn from prefab.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Multiblock::Encode_MultiblockExtensions(const flecs::entity& E, TArray<uint8>& Out)
{
	const FMultiblockExtensions* C = E.try_get<FMultiblockExtensions>();
	checkf(C, TEXT("Encode_MultiblockExtensions: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<uint8>(Ar, C->PortCount);

	for (int32 i = 0; i < kPortOccupantsLen; ++i)
	{
		const uint32 OccIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(C->PortOccupants[i]));
		SaveValue<uint32>(Ar, OccIdx);
	}
}

bool FlecsSaveEncoders_Multiblock::Decode_MultiblockExtensions(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_MultiblockExtensions: unknown version %u"), Version);
		return false;
	}

	FMultiblockExtensions C;
	Ar << C.PortCount;

	for (int32 i = 0; i < kPortOccupantsLen; ++i)
	{
		uint32 OccIdx = 0xFFFFFFFFu;
		Ar << OccIdx;
		C.PortOccupants[i] = static_cast<int64>(FlecsSaveRemap::ResolveSaveIndex(OccIdx));
	}

	E.set<FMultiblockExtensions>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// AUTO-REGISTRATION
// ═══════════════════════════════════════════════════════════════

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_MultiblockChildren, FMultiblockChildren, 1,
	FlecsSaveEncoders_Multiblock::Encode_MultiblockChildren,
	FlecsSaveEncoders_Multiblock::Decode_MultiblockChildren)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_MultiblockChildOf, FMultiblockChildOf, 1,
	FlecsSaveEncoders_Multiblock::Encode_MultiblockChildOf,
	FlecsSaveEncoders_Multiblock::Decode_MultiblockChildOf)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_MultiblockExtensions, FMultiblockExtensions, 1,
	FlecsSaveEncoders_Multiblock::Encode_MultiblockExtensions,
	FlecsSaveEncoders_Multiblock::Decode_MultiblockExtensions)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagMultiblockPart, FTagMultiblockPart)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagMultiblockAnchor, FTagMultiblockAnchor)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagMultiblockBonded, FTagMultiblockBonded)
