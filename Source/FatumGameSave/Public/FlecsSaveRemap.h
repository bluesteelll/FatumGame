// FlecsSaveRemap — thread-local entity-id remap table used during snapshot read.
//
// Encoders write entity references as `uint32 SaveIndex` (position in the per-operation
// entity table, populated by the walker — independent of the volatile Flecs entity_t
// which is recycled across runs). On load, the reader's Pass 0 creates fresh entities
// and fills `GRemapTable[SaveIndex] = NewEntityId`. Pass 1's decoders then read each
// SaveIndex back and call `ResolveSaveIndex` to convert it to the new Flecs entity_t
// (or to flecs::entity_t{0} for entities that were skipped due to a missing prefab).
//
// Per v1 §"Pass 2 (remap)": this avoids running decoders twice — refs decode once,
// already pointing at the new entity ids, because the remap table is filled BEFORE
// the component-decode pass starts.
//
// thread_local is correct here because the entire load apply runs on the sim thread
// inside a single EnqueueSeqCommand lambda. The pointer is set at the top of that
// lambda and cleared before it returns. No nested loads, no cross-thread access.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"

// Forward declare the Flecs typedef without dragging the full header into the public API.
namespace flecs
{
	using entity_t = uint64;
}

namespace FlecsSaveRemap
{
	/** Forward map: SaveIndex (uint32) → new flecs::entity_t (filled in Pass 0).
	 *  inline thread_local (C++17) — every TU shares the same per-thread instance,
	 *  and avoids the MSVC C2492 (thread_local + dllexport) restriction. */
	inline thread_local TMap<uint32, flecs::entity_t>* GRemapTable = nullptr;

	/** Reverse map: flecs::entity_t (current sim-thread id) → SaveIndex. Used by the
	 *  writer to encode cross-entity refs at walk time. Distinct from GRemapTable —
	 *  the writer never reads GRemapTable and the reader never reads GReverseMap. */
	inline thread_local const TMap<flecs::entity_t, uint32>* GReverseMap = nullptr;

	/** Reader-side helper. Resolves a saved index to its live entity id. Returns 0
	 *  (broken-entity sentinel) if the index doesn't map — caller decides whether
	 *  that's an error (mandatory ref) or a benign clear (optional ref). */
	FORCEINLINE flecs::entity_t ResolveSaveIndex(uint32 SaveIndex)
	{
		if (!GRemapTable)
		{
			// thread_local not initialised — decoder ran outside a load apply.
			// This is a contract violation; surface immediately.
			checkNoEntry();
			return 0;
		}
		if (const flecs::entity_t* Found = GRemapTable->Find(SaveIndex))
		{
			return *Found;
		}
		return 0;
	}

	/** Writer-side helper. Converts a live entity id (from .id() during walk) into
	 *  the SaveIndex assigned by the walker. Returns 0xFFFFFFFF if the entity is not
	 *  in the save set (orphan ref to a dead/skipped entity). */
	FORCEINLINE uint32 EntityToSaveIndex(flecs::entity_t LiveEntityId)
	{
		if (!GReverseMap)
		{
			checkNoEntry();
			return 0xFFFFFFFFu;
		}
		if (const uint32* Found = GReverseMap->Find(LiveEntityId))
		{
			return *Found;
		}
		return 0xFFFFFFFFu;
	}
}
