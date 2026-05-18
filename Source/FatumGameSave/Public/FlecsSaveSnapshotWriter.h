// FlecsSaveSnapshotWriter — sim-thread walker that produces an uncompressed payload buffer.
//
// PHASE 1: stub writer. Emits payload header + zero entities + payload footer (smoke-test
// the pipeline end-to-end). Real entity walking + per-component encoding land in Phase 2.
//
// Lifetime: heap-allocated via TSharedPtr (per v2 §5.11). Captured by sim-thread lambda;
// destructed on the game thread after WaitForSequence returns.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"

namespace flecs { struct world; }

/** Per-save snapshot writer. One instance per save request. */
class FATUMGAMESAVE_API FFlecsSaveSnapshotWriter
{
public:
	FFlecsSaveSnapshotWriter();
	~FFlecsSaveSnapshotWriter();

	/** Sim-thread entry point.
	 *
	 *  Walks the world (Phase 2+), serializes all save-worthy entities into SerializedBytes.
	 *
	 *  PHASE 1: writes only payload header (16B) + footer (8B) with EntityCount=0.
	 *  No interaction with the Flecs world is performed. The world pointer is accepted to
	 *  lock in the API surface that Phase 2 will use.
	 *
	 *  @param World Flecs world to snapshot (Phase 1: not read).
	 */
	void WalkAndSerialize(flecs::world* World);

	/** Game-thread accessor — returns the freshly serialized payload bytes (uncompressed).
	 *  Safe to call only AFTER WaitForSequence has confirmed the sim-thread walk completed. */
	const TArray<uint8>& GetSerializedBytes() const { return SerializedBytes; }

	/** Number of entity records in the payload. PHASE 1: always 0. */
	uint32 GetEntityCount() const { return EntityCountWritten; }

private:
	/** Backing storage for the serialized payload (pre-compression). */
	TArray<uint8> SerializedBytes;

	/** Number of entity records written. Phase 1 stub: always 0. */
	uint32 EntityCountWritten = 0;
};
