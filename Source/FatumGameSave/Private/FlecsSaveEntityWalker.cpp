// FlecsSaveEntityWalker — implementation. Sim thread only.

#include "FlecsSaveEntityWalker.h"

#include "FlecsSaveAssetPathTable.h"
#include "FlecsSaveComponentRegistry.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveTypeIds.h"

#include "FlecsEntityDefinition.h"

#include "FlecsEntityComponents.h"      // FEntityDefinitionRef
#include "FlecsGameTags.h"              // FTagDead, FTagProjectile, FTagDebrisFragment, FTagInteractable
#include "FlecsMeleeComponents.h"       // FTagMeleeAttacking
#include "FlecsExplosionComponents.h"   // FTagDetonate
#include "FlecsBarrageComponents.h"     // FBarrageBody, FTagCollision*, FTagCollisionPenetration

#include "flecs.h"

FFlecsSaveEntityWalker::FFlecsSaveEntityWalker() = default;

void FFlecsSaveEntityWalker::EnsureTagNameTable(flecs::world& /*World*/)
{
	if (bTagNameTableBuilt)
	{
		return;
	}

	const FFlecsSaveComponentRegistry& Registry = FFlecsSaveComponentRegistry::Get();
	Registry.ForEachDesc([this](const FlecsSave::FComponentDesc& Desc)
	{
		if (!Desc.bIsTag)
		{
			return;
		}
		// REGISTER_SAVE_TAG uses TEXT(#TagType) for DebugName — matches Flecs's default
		// type name (`World.component<FTagInteractable>()` → name() == "FTagInteractable").
		const FName NameKey(Desc.DebugName);
		TagNameToTypeId.Add(NameKey, Desc.TypeId);
	});

	bTagNameTableBuilt = true;

	UE_LOG(LogFlecsSave, Verbose,
		TEXT("FFlecsSaveEntityWalker: built TagNameToTypeId with %d entries"),
		TagNameToTypeId.Num());
}

// ─── Walker entry point ───────────────────────────────────────────────────────

// Internal helper — the candidate collect + sort used by both PreWalkCollectIds
// and Walk. Pure read-only; no encoder dispatch, no path table.
static void CollectCandidateIdsSorted(flecs::world& World, TArray<flecs::entity_t>& OutIds)
{
	OutIds.Reset();
	OutIds.Reserve(1024);

	auto CandidateQuery = World.query_builder<const FEntityDefinitionRef>()
		.with(flecs::Prefab).oper(flecs::Not)
		.without<FTagDead>()
		.without<FTagProjectile>()
		.without<FTagMeleeAttacking>()
		.without<FTagDetonate>()
		.without<FTagCollisionDamage>()
		.without<FTagCollisionPickup>()
		.without<FTagCollisionBounce>()
		.without<FTagCollisionDestructible>()
		.without<FTagCollisionCharacter>()
		.without<FTagCollisionFragmentation>()
		.without<FTagCollisionProcessed>()
		.without<FTagCollisionPenetration>()
		.build();

	CandidateQuery.each([&OutIds](flecs::entity Entity, const FEntityDefinitionRef& /*Ref*/)
	{
		OutIds.Add(Entity.id());
	});

	// Sort by entity id ascending for determinism (Jolt body add-order). The reverse
	// map built from this list MUST agree with what Walk() produces — sorting both
	// sides identically is the contract.
	OutIds.Sort();
}

void FFlecsSaveEntityWalker::PreWalkCollectIds(flecs::world& World, TArray<flecs::entity_t>& OutSortedIds)
{
	// Pure pre-walk: identical filter rules as Walk(), no encoder dispatch.
	// SkippedMissingDefRefCount is NOT updated here — Walk()'s audit query is
	// authoritative (deferred to the main walk to avoid duplicate Error spam).
	CollectCandidateIdsSorted(World, OutSortedIds);
}

void FFlecsSaveEntityWalker::Walk(
	flecs::world& World,
	FFlecsSaveAssetPathTable& PathTable,
	TArray<FEntityRecord>& OutRecords)
{
	OutRecords.Reset();
	SkippedMissingDefRefCount = 0;
	EnsureTagNameTable(World);

	// ─────────────────────────────────────────────────────────────────────────
	// PASS A: collect candidate entity ids (no encoding yet — keep iteration
	// pure so we can sort for determinism before touching any components).
	// ─────────────────────────────────────────────────────────────────────────
	TArray<flecs::entity_t> CandidateIds;
	CollectCandidateIdsSorted(World, CandidateIds);

	// ─── Audit (v2 §M3): catch save-worthy entities missing FEntityDefinitionRef.
	// Any entity with a physics body that's not in the excluded set MUST carry the ref.
	{
		auto MissingRefQuery = World.query_builder<>()
			.with<FBarrageBody>()
			.without<FEntityDefinitionRef>()
			.with(flecs::Prefab).oper(flecs::Not)
			.without<FTagDead>()
			.without<FTagProjectile>()
			.without<FTagMeleeAttacking>()
			.without<FTagDetonate>()
			.without<FTagCollisionDamage>()
			.without<FTagCollisionPickup>()
			.without<FTagCollisionBounce>()
			.without<FTagCollisionDestructible>()
			.without<FTagCollisionCharacter>()
			.without<FTagCollisionFragmentation>()
			.without<FTagCollisionProcessed>()
			.without<FTagCollisionPenetration>()
			.without<FTagDebrisFragment>()
			.build();

		MissingRefQuery.each([this](flecs::entity Entity)
		{
			++SkippedMissingDefRefCount;
			UE_LOG(LogFlecsSave, Error,
				TEXT("FFlecsSaveEntityWalker: save-worthy entity %llu has FBarrageBody but no FEntityDefinitionRef — skipping"),
				static_cast<uint64>(Entity.id()));
		});

#if !UE_BUILD_SHIPPING
		// Editor / Development: hard-stop. Shipping: log + skip (already done above).
		checkf(SkippedMissingDefRefCount == 0,
			TEXT("FFlecsSaveEntityWalker: %d save-worthy entities lacked FEntityDefinitionRef (see Error log). "
			     "All persistently-spawned entities MUST set FEntityDefinitionRef at spawn — per v2 §M3."),
			SkippedMissingDefRefCount);
#endif
	}

	// ─────────────────────────────────────────────────────────────────────────
	// PASS B: encode each entity in stable order. SaveIndex == position.
	// ─────────────────────────────────────────────────────────────────────────
	OutRecords.Reserve(CandidateIds.Num());

	for (int32 i = 0; i < CandidateIds.Num(); ++i)
	{
		flecs::entity Entity(World, CandidateIds[i]);
		if (!Entity.is_alive())
		{
			// Race-window guard: should not happen under the save fence, but cheap.
			UE_LOG(LogFlecsSave, Warning,
				TEXT("FFlecsSaveEntityWalker: entity %llu died between candidate-collect and encode — skipping"),
				static_cast<uint64>(CandidateIds[i]));
			continue;
		}

		FEntityRecord& Record = OutRecords.AddDefaulted_GetRef();
		Record.OriginalId = Entity.id();
		Record.SaveIndex = static_cast<uint32>(OutRecords.Num() - 1);
		EncodeOneEntity(Entity, PathTable, Record);
	}

	UE_LOG(LogFlecsSave, Log,
		TEXT("FFlecsSaveEntityWalker::Walk: encoded %d entities (%d candidates pre-sort)"),
		OutRecords.Num(), CandidateIds.Num());
}

// ─── Per-entity encode ────────────────────────────────────────────────────────

void FFlecsSaveEntityWalker::EncodeOneEntity(
	flecs::entity Entity,
	FFlecsSaveAssetPathTable& PathTable,
	FEntityRecord& Out)
{
	// Definition is guaranteed by the candidate query (filters on FEntityDefinitionRef).
	const FEntityDefinitionRef* DefRef = Entity.try_get<FEntityDefinitionRef>();
	checkf(DefRef && DefRef->Definition,
		TEXT("FFlecsSaveEntityWalker::EncodeOneEntity: entity %llu missing FEntityDefinitionRef despite query filter"),
		static_cast<uint64>(Entity.id()));
	Out.Definition = DefRef->Definition;
	Out.PathTableIndex = PathTable.RegisterPath(DefRef->Definition);

	const FFlecsSaveComponentRegistry& Registry = FFlecsSaveComponentRegistry::Get();

	// ─────────────────────────────────────────────────────────────────────────
	// Walk every (component) id on this entity. flecs::id can be:
	//   - pair (skip — Phase 2 has no pair encoders)
	//   - wildcard (skip — internal only)
	//   - entity (= component or tag — dispatch)
	// Per v2 §M8: this is the ONLY safe way to enumerate tags. NEVER `each<FTag>`.
	// ─────────────────────────────────────────────────────────────────────────
	Entity.each([this, Entity, &Out, &Registry](flecs::id Id)
	{
		if (!Id.is_entity() || Id.is_wildcard())
		{
			return;
		}

		const flecs::entity TypeEntity = Id.entity();
		const flecs::string_view NameView = TypeEntity.name();
		const char* RawName = NameView.c_str();
		if (!RawName || RawName[0] == '\0')
		{
			return;
		}

		const FName NameKey(RawName);

		// Tag first — most ids on a typical entity are tags (cheap O(1) hit).
		if (const uint16* TagTypeId = TagNameToTypeId.Find(NameKey))
		{
			Out.TagIds.Add(*TagTypeId);
			return;
		}

		// Non-tag component lookup. Unknown names (Flecs internals, runtime helpers
		// like FCollisionPair without a save encoder) are silently skipped.
		const FlecsSave::FComponentDesc* Desc = Registry.FindByDebugName(NameKey);
		if (!Desc || Desc->bIsTag)
		{
			return;
		}

		// FEntityDefinitionRef is special-cased: serialized via the per-entity-header
		// path index, NOT as a component block. The registry registers it so the
		// reader knows the TypeId is "valid but header-only".
		if (Desc->TypeId == FlecsSaveTypeIds::kTypeId_EntityDefinitionRef)
		{
			return;
		}

		checkf(Desc->Encoder != nullptr,
			TEXT("FFlecsSaveEntityWalker: encoder missing for TypeId 0x%04X ('%s')"),
			Desc->TypeId, Desc->DebugName);

		FEntityRecord::FComponentBlock Block;
		Block.TypeId = Desc->TypeId;
		Desc->Encoder(Entity, Block.Bytes);
		Out.Components.Add(MoveTemp(Block));
	});
}
