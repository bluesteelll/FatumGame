// FlecsSaveEncoders_Multiblock — encoder/decoder declarations for Multiblock-domain components.
//
// Phase 4 set (TypeIds 0x0700–0x0702 + tags 0x1700–0x1702):
//   FMultiblockChildren            — anchor-side roster of bonded child slots
//                                    (ChildSlots[15] each REMAP + cached metadata),
//                                    ChildCount, AnchorYawSnappedDeg.
//   FMultiblockChildOf             — child-side back ref. AnchorEntityId REMAP.
//   FMultiblockExtensions          — per-station extension-port occupants
//                                    (PortOccupants[8] each REMAP) + PortCount.
//
// Tags (registered via REGISTER_SAVE_TAG):
//   FTagMultiblockPart, FTagMultiblockAnchor, FTagMultiblockBonded.
//
// Cross-entity refs use FlecsSaveRemap::EntityToSaveIndex on write +
// FlecsSaveRemap::ResolveSaveIndex on read.
//
// SCOPE NOTE: FMultiblockChildSlot lives INLINE inside FMultiblockChildren.
// FMultiblockPartStatic / FConnectorSegmentStatic are prefab-side and not saved.
// FPendingPartAttach / FPendingStationAttach are excluded by design — 5-tick
// in-flight commands per Phase 4 spec.

#pragma once

#include "CoreMinimal.h"

namespace flecs { struct entity; }

namespace FlecsSaveEncoders_Multiblock
{
	void Encode_MultiblockChildren(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_MultiblockChildren(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_MultiblockChildOf(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_MultiblockChildOf(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_MultiblockExtensions(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_MultiblockExtensions(const flecs::entity& E, const uint8* Bytes, uint32 Len);
}
