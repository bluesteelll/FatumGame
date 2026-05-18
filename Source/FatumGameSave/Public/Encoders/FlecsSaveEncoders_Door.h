// FlecsSaveEncoders_Door — encoders for door-domain components (Phase 5).
//
// Components:
//   FDoorInstance      (TypeId 0x0500) — door state machine + auto-close timer +
//                                         target position + open direction.
//                                         ConstraintKey NOT serialized (DoorSystem
//                                         regenerates from FDoorStatic on first tick
//                                         post-load).
//   FDoorTriggerLink   (TypeId 0x0501) — link from trigger entity to door's BarrageKey.
//                                         Encoded as door entity SaveIndex (REMAP);
//                                         decoder leaves TargetDoorKey = 0 and relies
//                                         on a post-load pass to resolve via the new
//                                         door entity's FBarrageBody (Phase 6/7).
//
// Tags:
//   FTagDoor           (TypeId 0x1500)
//   FTagDoorTrigger    (TypeId 0x1501)

#pragma once

#include "CoreMinimal.h"

namespace flecs { struct entity; }

namespace FlecsSaveEncoders_Door
{
	void Encode_DoorInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_DoorInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_DoorTriggerLink(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_DoorTriggerLink(const flecs::entity& E, const uint8* Bytes, uint32 Len);
}
