// FlecsSaveSnapshotReader — applies a decoded payload blob to the live Flecs world.
//
// PHASE 1: stub reader. Validates payload header + footer + EntityCount=0. Phase 2 will
// implement entity reconstruction (create entities via IsA(prefab), decode components,
// build OldIndex→NewEntity remap, restore Barrage bodies, etc.).
//
// Lifetime: heap-allocated via TSharedPtr (per v2 §5.11 + C2 fix). Captured by sim-thread
// lambda via TSharedPtr value-copy. Destructed on whichever thread releases the last ref.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"

namespace flecs { struct world; }

/** Per-load snapshot reader. One instance per load request. */
class FATUMGAMESAVE_API FFlecsSaveSnapshotReader
{
public:
	/** Construct from a decompressed payload blob. The reader takes a defensive copy so
	 *  the caller is free to release the source buffer immediately. */
	explicit FFlecsSaveSnapshotReader(TArray<uint8> InPayloadBytes);

	~FFlecsSaveSnapshotReader();

	/** Sim-thread entry point — apply the payload to the world.
	 *
	 *  PHASE 1: parses and validates the payload header + footer only. EntityCount MUST
	 *  be 0 (anything else triggers an ensure in Phase 1 because we have no decoders yet).
	 *
	 *  @param World Flecs world to populate (Phase 1: not mutated).
	 *  @return true on success, false on malformed payload.
	 */
	bool ApplyToFlecsWorld(flecs::world* World);

	/** Total entities in the payload. PHASE 1 always 0. */
	uint32 GetEntityCount() const { return EntityCountInPayload; }

private:
	/** Defensive copy of the payload bytes. */
	TArray<uint8> PayloadBytes;

	/** Parsed from the payload header on ApplyToFlecsWorld. 0 in Phase 1. */
	uint32 EntityCountInPayload = 0;
};
