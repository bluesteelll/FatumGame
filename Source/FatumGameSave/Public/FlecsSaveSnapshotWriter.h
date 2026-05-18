// FlecsSaveSnapshotWriter — sim-thread walker that produces an uncompressed payload buffer.
//
// PHASE 2: real walker. Per-operation AssetPathTable owned as member (per v2 §M11).
// Writes payload as: header → asset-path table → entity records → footer.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"

#include "FlecsSaveAssetPathTable.h"
#include "FlecsSaveEntityWalker.h"

namespace flecs { struct world; }

/** Per-save snapshot writer. One instance per save request. */
class FATUMGAMESAVE_API FFlecsSaveSnapshotWriter
{
public:
	FFlecsSaveSnapshotWriter();
	~FFlecsSaveSnapshotWriter();

	/** Sim-thread entry point.
	 *
	 *  Walks the world, serializes all save-worthy entities into SerializedBytes.
	 *  Layout per v1 §"Payload framing":
	 *    [PayloadHeader 16B]
	 *    [Asset path table — uint32 count + per-entry { uint32 len, UTF-8 bytes, pad }]
	 *    [Entity records — header + tags + components per entity]
	 *    [PayloadFooter 8B]
	 *
	 *  @param World Flecs world to snapshot.
	 */
	void WalkAndSerialize(flecs::world* World);

	/** Game-thread accessor — returns the freshly serialized payload bytes (uncompressed).
	 *  Safe to call only AFTER WaitForSequence has confirmed the sim-thread walk completed. */
	const TArray<uint8>& GetSerializedBytes() const { return SerializedBytes; }

	/** Number of entity records in the payload. */
	uint32 GetEntityCount() const { return EntityCountWritten; }

private:
	/** Backing storage for the serialized payload (pre-compression). */
	TArray<uint8> SerializedBytes;

	/** Per-operation prefab path table. NOT a singleton (v2 §M11). */
	FFlecsSaveAssetPathTable PathTable;

	/** Walker — populates EntityRecords during WalkAndSerialize. */
	FFlecsSaveEntityWalker Walker;

	/** Walker output — held briefly between walk and serialize. */
	TArray<FEntityRecord> EntityRecords;

	/** Number of entity records written. */
	uint32 EntityCountWritten = 0;
};
