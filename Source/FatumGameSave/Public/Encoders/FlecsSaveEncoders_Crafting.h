// FlecsSaveEncoders_Crafting — encoder/decoder declarations for Crafting-domain components.
//
// Phase 4 set (TypeIds 0x0600–0x0606 + tags 0x1600–0x1602):
//   FCraftingStationInstance       — fuel reservoir + digest cache. MatchedRecipe pointer
//                                    is NOT serialized; decoder leaves it nullptr per
//                                    v2 §M6 (RecipeMatchSystem re-derives on first sim
//                                    tick from slot contents).
//   FCraftingSlots                 — fixed 24-slot table (each SlotEntityId REMAP) + role counts.
//   FFuelSlot                      — denormalized fuel-slot entity id (REMAP).
//   FCraftingSlotBackRef           — StationEntityId REMAP + SlotIndex + Role + OwningPortIndex.
//   FSmelterInstance               — Phase / progress / DurationCached / start+cancel request
//                                    flags / ConsumedLedger (each entry stores a path-table index
//                                    for its Definition pointer, plus Count + SourceSlotIndex).
//   FCraftingSlotLockedByStation   — OwningStationEntityId (REMAP).
//   FStationEffectiveLayout        — derived layout deltas (intrinsic fields only).
//
// Tags (registered via REGISTER_SAVE_TAG):
//   FTagCraftingStation, FTagCraftingFuel, FTagStationDisabled.
//
// Cross-entity refs use FlecsSaveRemap::EntityToSaveIndex on write +
// FlecsSaveRemap::ResolveSaveIndex on read. Recipe Definition refs (ConsumedLedger
// rows) use FlecsSaveRemap::GPathTable->RegisterPath / ResolveDefinition since the
// remap is for live entities only.
//
// SCOPE NOTE: prefab-side statics (FCraftingStationStatic, FCraftingFuelItemData)
// are never saved — the walker excludes prefabs via .with(flecs::Prefab).oper(flecs::Not).
// Pending transient components (FPendingPartAttach, FPendingStationAttach,
// FPendingConnectorPlace) are intentionally NOT registered: per the Phase 4 spec
// they are treated as drained pre-save (5-tick in-flight commands).

#pragma once

#include "CoreMinimal.h"

namespace flecs { struct entity; }

namespace FlecsSaveEncoders_Crafting
{
	void Encode_CraftingStationInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_CraftingStationInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_CraftingSlots(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_CraftingSlots(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_FuelSlot(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_FuelSlot(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_CraftingSlotBackRef(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_CraftingSlotBackRef(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_SmelterInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_SmelterInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_CraftingSlotLockedByStation(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_CraftingSlotLockedByStation(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_StationEffectiveLayout(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_StationEffectiveLayout(const flecs::entity& E, const uint8* Bytes, uint32 Len);
}
