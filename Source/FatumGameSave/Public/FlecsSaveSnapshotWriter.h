// FlecsSaveSnapshotWriter — sim-thread walker that produces an uncompressed payload buffer.
//
// PHASE 2: real walker. Per-operation AssetPathTable owned as member (per v2 §M11).
// Writes payload as: header → asset-path table → entity records → footer.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

#include "FlecsSaveAssetPathTable.h"
#include "FlecsSaveEntityWalker.h"  // brings in flecs::entity_t typedef

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
	 *  Layout per v2 §"Payload framing" (Phase 7 — kPayloadVersion=2):
	 *    [PayloadHeader 16B]
	 *    [WorldName  — uint32 ByteLen + UTF-8 bytes + pad-to-4]
	 *    [Asset path table — uint32 count + per-entry { uint32 len, UTF-8 bytes, pad }]
	 *    [Entity records — header + tags + components per entity]
	 *    [PayloadFooter 8B]
	 *
	 *  @param World Flecs world to snapshot.
	 *  @param WorldName Map name captured by the caller on the game thread BEFORE the
	 *         sim-thread dispatch (PIE prefix already stripped via UWorld::RemovePIEPrefix).
	 *         Sim thread MUST NOT touch UWorld* APIs to derive this — caller passes by value.
	 */
	void WalkAndSerialize(flecs::world* World, const FString& WorldName);

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

	/** Reverse map (entity_t → SaveIndex). Pointed at by FlecsSaveRemap::GReverseMap
	 *  during the entire walk-and-encode operation so encoders can resolve cross-
	 *  entity refs. Populated by the pre-walk in WalkAndSerialize. Member (not stack
	 *  local) so it remains alive across the encoder loop AND across the WriteEntityRecord
	 *  loop in case encoders mutate it (they don't, but keeping the storage longer-lived
	 *  is cheap). flecs::entity_t is forward-declared as uint64 by FlecsSaveEntityWalker.h. */
	TMap<flecs::entity_t, uint32> ReverseMapStorage;

	/** Number of entity records written. */
	uint32 EntityCountWritten = 0;
};
