// FlecsSaveEntityWalker — sim-thread walker producing FEntityRecord array.
//
// Enumerates save-worthy entities (excludes prefabs, FTagDead, FTagProjectile,
// FTagMeleeAttacking, FTagCollision*), sorts by Flecs entity_t ascending (Jolt body
// add-order determinism, per v2 §M3 + risk #7), then encodes per-entity tag list
// and component blocks via the FFlecsSaveComponentRegistry.
//
// Phase 2 emits NO Barrage body block (bit0 cleared) and NO spawner-guid block
// (bit1 cleared); both are wired in Phase 5.
//
// Per v2 §M8: tag enumeration uses `entity.each(flecs::id)` walk + name→TypeId map
// (built once at walker construction from the registry's tag descriptors). NEVER
// iterates via `world.each<FTag>` (zero-size tag crash).

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

#include "FlecsSaveBarrageRestore.h"  // FBarrageBodyState (per-record Phase 5 block)

class UFlecsEntityDefinition;
class FFlecsSaveAssetPathTable;

// Forward declare entity_t to avoid pulling flecs.h into the public header.
namespace flecs
{
	using entity_t = uint64;
	struct world;
	struct entity;
}

/** POD record produced by the walker for one save-worthy entity. Per v1 §2 line 382-397. */
struct FATUMGAMESAVE_API FEntityRecord
{
	/** Diagnostic-only — sim-thread entity_t from save-time. DO NOT use as identity
	 *  post-load (Flecs recycles ids). Persistence identity is FEntityDefinitionRef
	 *  + FSpawnerProvenance (the latter added in Phase 5). */
	flecs::entity_t OriginalId = 0;

	/** Position in the save-order array — also the SaveIndex written into the per-entity
	 *  header AND used as the cross-entity ref key in the remap table. */
	uint32 SaveIndex = 0xFFFFFFFFu;

	/** Pointer to the asset Definition (resolved at walk time). Always non-null —
	 *  walker checkf's any save-worthy entity missing FEntityDefinitionRef per v2 §M3. */
	UFlecsEntityDefinition* Definition = nullptr;

	/** Index into the writer's path table (FFlecsSaveAssetPathTable) — written into
	 *  the per-entity header. */
	uint32 PathTableIndex = 0xFFFFFFFFu;

	// ─── Phase 5: Barrage body block (bit 0 of Flags) ─────────────────────
	// Set during Walk for entities that carry FBarrageBody and whose primitive is
	// alive at snapshot time. SpawnerGuid (bit 1) was dropped in v2 §C9 in favor of
	// FSpawnerProvenance — that's a regular component now, not a header block.

	/** True if this entity carried a live FBarrageBody at snapshot time. */
	bool bHasBarrageBody = false;

	/** 88-byte body snapshot. Only valid when bHasBarrageBody == true. */
	FBarrageBodyState BarrageBody;

	/** Registered tag TypeIds present on this entity, in registration order. */
	TArray<uint16> TagIds;

	/** One block per registered non-tag component present on this entity.
	 *  Bytes are version-prefixed (encoder writes uint16 Version then payload). */
	struct FComponentBlock
	{
		uint16 TypeId = 0;
		TArray<uint8> Bytes;
	};
	TArray<FComponentBlock> Components;
};

/** Walker — instantiated once per save operation by the snapshot writer. */
class FATUMGAMESAVE_API FFlecsSaveEntityWalker
{
public:
	FFlecsSaveEntityWalker();
	~FFlecsSaveEntityWalker() = default;

	/** Sim-thread entry point. Iterates save-worthy entities, sorts by id ascending,
	 *  populates OutRecords. PathTable is also populated (RegisterPath called per
	 *  recorded definition). Records are emitted in stable ascending order.
	 *
	 *  Skips (with Verbose log) entities matching any of:
	 *    - prefab flag
	 *    - FTagDead, FTagProjectile, FTagMeleeAttacking, FTagCollision*, FTagDetonate
	 *    - no FEntityDefinitionRef (Editor: checkf; Shipping: log + skip)
	 */
	void Walk(flecs::world& World, FFlecsSaveAssetPathTable& PathTable, TArray<FEntityRecord>& OutRecords);

	/** Sim-thread pre-walk: enumerate candidate entity ids (sorted ascending) WITHOUT
	 *  encoding. Used by the snapshot writer to populate FlecsSaveRemap::GReverseMap
	 *  BEFORE the main Walk dispatches encoders that may reference cross-entity ids.
	 *
	 *  Filter rules match Walk() exactly, but no encoder dispatch / no path-table
	 *  registration happens. OutSortedIds[i] is the entity assigned SaveIndex==i.
	 *
	 *  Idempotent: callable multiple times, deterministic output given an unmodified
	 *  world. Snapshot fence guarantees the world is stable between this call and
	 *  the subsequent Walk(); a sanity-check in WalkAndSerialize asserts the count
	 *  matches. */
	void PreWalkCollectIds(flecs::world& World, TArray<flecs::entity_t>& OutSortedIds);

	/** Diagnostic: how many entities were skipped due to missing FEntityDefinitionRef.
	 *  Editor builds checkf if this is non-zero AND the offending entities lack the
	 *  exclusion tags above (programming error to forget the ref). */
	int32 GetSkippedMissingDefRefCount() const { return SkippedMissingDefRefCount; }

private:
	/** Encode one entity: tag walk + component dispatch. PathTable.RegisterPath called here. */
	void EncodeOneEntity(flecs::entity Entity, FFlecsSaveAssetPathTable& PathTable, FEntityRecord& Out);

	/** Build TagNameToTypeId lazily on first walk — maps Flecs type entity name() to
	 *  registered SaveTypeId for fast lookup during entity.each(flecs::id). */
	void EnsureTagNameTable(flecs::world& World);

	// FName-keyed because Flecs name() returns const char* with a stable lifetime;
	// FName avoids storing the raw pointer across walks.
	TMap<FName, uint16> TagNameToTypeId;
	bool bTagNameTableBuilt = false;

	int32 SkippedMissingDefRefCount = 0;
};
