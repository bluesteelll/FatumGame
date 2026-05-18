// FlecsSaveAssetPathTable — per-operation prefab asset path collector / resolver.
//
// Per v2 §M11: writer and reader EACH own an instance of this table (NOT a singleton —
// global state races between game and pool threads). Writer accumulates paths during
// the entity walk and emits them as a string-table section at the start of the payload.
// Reader deserializes that string table and resolves indices to UFlecsEntityDefinition*
// via TSoftObjectPtr::LoadSynchronous when each entity needs its prefab.
//
// Indices are uint32; 0xFFFFFFFF is the "no path" sentinel for entities that lack
// FEntityDefinitionRef (skipped by the walker but the constant is defined here so the
// invariant is centralised).

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "UObject/SoftObjectPtr.h"

class UFlecsEntityDefinition;
class FArchive;

/** Per-operation prefab path table. NOT thread-safe and NOT a singleton — one instance
 *  per writer / reader. */
class FATUMGAMESAVE_API FFlecsSaveAssetPathTable
{
public:
	/** Sentinel for "no associated asset path" (entity has no FEntityDefinitionRef). */
	static constexpr uint32 kInvalidIndex = 0xFFFFFFFFu;

	FFlecsSaveAssetPathTable() = default;
	~FFlecsSaveAssetPathTable() = default;

	// Non-copyable / non-movable — caller passes the table by reference to writer/reader.
	FFlecsSaveAssetPathTable(const FFlecsSaveAssetPathTable&) = delete;
	FFlecsSaveAssetPathTable& operator=(const FFlecsSaveAssetPathTable&) = delete;

	// ─── WRITER-SIDE API ─────────────────────────────────────────────────────

	/** Register a definition asset and return its dedup index. Returns kInvalidIndex
	 *  if Definition is nullptr — caller must NOT call this for entities lacking a
	 *  definition ref; that's a bug surfaced by the walker's checkf, not handled here. */
	uint32 RegisterPath(const UFlecsEntityDefinition* Definition);

	/** Serialize the table into the archive (writer-side string-table section).
	 *  Format: uint32 AssetCount, then per entry: { uint32 PathLen, UTF-8 bytes, pad-to-4 }.
	 *  All entries are non-empty (kInvalidIndex slots are never serialized). */
	void Serialize(FArchive& Ar);

	/** Number of registered paths (writer-side accessor for diagnostics). */
	int32 Num() const { return PathStrings.Num(); }

	// ─── READER-SIDE API ─────────────────────────────────────────────────────

	/** Deserialize the table from the archive (reader-side string-table section).
	 *  Pre-resolves soft pointers to TSoftObjectPtr but defers the actual asset load
	 *  to ResolveDefinition() so unreferenced rows don't pay the I/O cost. */
	void Deserialize(FArchive& Ar);

	/** Reader-side resolve. Returns nullptr if Index is out of range or the asset cannot
	 *  be loaded (in which case the caller MUST log + skip the entity per v1 §7).
	 *  Internally caches the resolved pointer to avoid re-loading on repeat lookups. */
	UFlecsEntityDefinition* ResolveDefinition(uint32 Index);

private:
	// Writer side: definition → assigned index, plus the parallel string array we serialize.
	TMap<TWeakObjectPtr<const UFlecsEntityDefinition>, uint32> DefinitionToIndex;
	TArray<FString> PathStrings;

	// Reader side: parallel arrays of soft pointers + cached resolved pointers (lazy load).
	TArray<TSoftObjectPtr<UFlecsEntityDefinition>> SoftPointers;
	TArray<TObjectPtr<UFlecsEntityDefinition>> ResolvedCache;
};

// ─────────────────────────────────────────────────────────────────────────────
// Thread-local active-table pointer for encoders (Phase 4+).
//
// Mirrors the FlecsSaveRemap::GRemapTable / GReverseMap pattern: encoders that
// need asset-path resolution (e.g. FSmelterInstance::ConsumedLedger entries
// reference UFlecsEntityDefinition* which can't go through the entity remap)
// read/write paths through this active table.
//
// Writer sets this BEFORE calling Walker.Walk (so Encode_* can call
// GPathTable->RegisterPath); Reader sets this BEFORE the Pass-1 component-decode
// loop (so Decode_* can call GPathTable->ResolveDefinition). Both clear it via
// ON_SCOPE_EXIT to satisfy the contract that GPathTable is only non-null inside
// an active save/load operation on the sim thread.
//
// inline thread_local (C++17) — every TU shares the same per-thread instance,
// and avoids the MSVC C2492 (thread_local + dllexport) restriction. Same
// rationale as FlecsSaveRemap::GRemapTable.
namespace FlecsSaveRemap
{
	inline thread_local FFlecsSaveAssetPathTable* GPathTable = nullptr;
}
