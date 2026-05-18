// FlecsSaveSnapshotReader — applies a decoded payload blob to the live Flecs world.
//
// PHASE 2: real reader. Two-pass decode:
//   Pass 0: parse asset table; per record create new entity via IsA(prefab); fill
//           OldSaveIndex → NewEntity remap table (thread_local).
//   Pass 1: per record, dispatch tag adds + component decoders (decoders read refs
//           via FlecsSaveRemap::ResolveSaveIndex which the table set in Pass 0).
//
// Lifetime: heap-allocated via TSharedPtr (per v2 §5.11 + C2 fix). Captured by sim-thread
// lambda via TSharedPtr value-copy. Destructed on whichever thread releases the last ref.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

#include "FlecsSaveAssetPathTable.h"

namespace flecs
{
	using entity_t = uint64;
	struct world;
}

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
	 *  @param World Flecs world to populate.
	 *  @return true on success (header valid, all entities decoded — individual entity
	 *          skips for missing prefab assets are logged but don't fail the whole load).
	 */
	bool ApplyToFlecsWorld(flecs::world* World);

	/** Total entities in the payload (set after ApplyToFlecsWorld parses the header). */
	uint32 GetEntityCount() const { return EntityCountInPayload; }

	/** Diagnostic: how many entities were skipped due to missing prefab asset on load. */
	int32 GetSkippedMissingPrefabCount() const { return SkippedMissingPrefabCount; }

	/** Phase 5 spawner-dedup helper. Walks the payload (without applying anything to the
	 *  Flecs world) and collects every FSpawnerProvenance tuple in the saved entities.
	 *  Game-thread safe; called BEFORE ApplyToFlecsWorld so the save subsystem can mark
	 *  matching AFlecsEntitySpawner actors with bSavedEntityOverridesMe pre-BeginPlay.
	 *  @return true on success (header parsed); false if payload is malformed. */
	bool CollectSavedSpawnerProvenance(TSet<TPair<FName, FName>>& OutTuples);

private:
	/** Defensive copy of the payload bytes. */
	TArray<uint8> PayloadBytes;

	/** Per-operation prefab path table (deserialized from payload). */
	FFlecsSaveAssetPathTable PathTable;

	/** Pass-0 remap table — populated as entities are created, consumed by Pass-1 decoders. */
	TMap<uint32, flecs::entity_t> RemapTable;

	/** Parsed from the payload header. */
	uint32 EntityCountInPayload = 0;

	/** Diagnostic counter for entities skipped due to missing prefab asset. */
	int32 SkippedMissingPrefabCount = 0;
};
