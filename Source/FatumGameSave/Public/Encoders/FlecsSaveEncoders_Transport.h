// FlecsSaveEncoders_Transport — encoder/decoder declarations for Transport-domain components.
//
// Phase 4 set (TypeIds 0x0703–0x0704 + tags 0x1703–0x1704):
//   FConnectorPlaced   — placed-segment runtime state. Snap targets REMAP. NetworkId
//                        regenerated on first sim tick post-load by NetworkRebuildSystem.
//   FStationPorts      — variable-length per-station port table (TArray<FPortSlot>),
//                        each FPortSlot contains a ConnectedSegmentId REMAP plus
//                        intrinsic Kind / SocketName / NetworkId / AcceptPriority /
//                        LocalOffsetCm. NetworkId also regenerated post-load.
//
// Tags (registered via REGISTER_SAVE_TAG):
//   FTagConnectorSegment, FTagConnectorPlaced.
//
// SCOPE NOTE: FConnectorSegmentStatic is prefab-side and not saved.
// FPortSlot is INLINE-ONLY inside FStationPorts.Ports[] — never registered as a
// standalone Flecs component. FPendingConnectorPlace is excluded (5-tick in-flight
// command per Phase 4 spec).

#pragma once

#include "CoreMinimal.h"

namespace flecs { struct entity; }

namespace FlecsSaveEncoders_Transport
{
	void Encode_ConnectorPlaced(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_ConnectorPlaced(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_StationPorts(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_StationPorts(const flecs::entity& E, const uint8* Bytes, uint32 Len);
}
