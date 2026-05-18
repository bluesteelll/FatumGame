// FlecsSaveSnapshotReader — Phase 2 implementation. Two-pass entity decode.

#include "FlecsSaveSnapshotReader.h"

#include "FlecsSaveBarrageRestore.h"
#include "FlecsSaveComponentRegistry.h"
#include "FlecsSaveFileFormat.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveRemap.h"
#include "FlecsSaveTypeIds.h"

#include "FlecsEntityDefinition.h"
#include "FlecsArtillerySubsystem.h"

#include "BarrageDispatch.h"  // typed pointer for RestoreBarrageBodyForEntity

#include "Misc/ScopeExit.h"
#include "Serialization/MemoryReader.h"

#include "flecs.h"

namespace
{
	/** Per-record offset table — built during Pass 0 so Pass 1 can re-seek to read
	 *  tags + components after the entity is created. */
	struct FRecordOffset
	{
		int64 RecordStartOffset = 0;  // start of EntityRecordSize field
		uint32 RecordSize = 0;
		uint32 SaveIndex = 0;
		uint32 PathTableIndex = 0;
		uint8 ComponentCount = 0;
		uint8 TagCount = 0;
		uint8 Flags = 0;
		int64 TagBlockOffset = 0;     // start of tag IDs
		int64 BarrageBodyOffset = 0;  // start of 88-byte body block (valid when Flags & 0x01)
		int64 ComponentBlockOffset = 0; // start of first component block
	};

	/** Read per-entity header AND record where the tag/component blocks start. Returns
	 *  false on malformed header. Advances Reader past the header (and any Phase 5
	 *  GUID/Barrage blocks when those bits are set — Phase 2 always 0). */
	bool ReadEntityHeader(FMemoryReader& Reader, FRecordOffset& Out)
	{
		Out.RecordStartOffset = Reader.Tell();

		Reader << Out.RecordSize;
		Reader << Out.SaveIndex;

		uint64 OriginalFlecsId = 0;
		Reader << OriginalFlecsId;
		// OriginalFlecsId is diagnostic-only per v1 m7 — discarded.

		Reader << Out.Flags;
		Reader << Out.ComponentCount;
		Reader << Out.TagCount;
		uint8 Pad = 0;
		Reader << Pad;
		Reader << Out.PathTableIndex;

		// Phase 5 blocks — neither bit is ever set in Phase 2 writers, but the reader
		// is forward-compatible (skips bytes when bits are set on a future-version blob).
		if (Out.Flags & 0x02)
		{
			// Spawner GUID = 16 bytes — skip.
			Reader.Seek(Reader.Tell() + 16);
		}

		Out.TagBlockOffset = Reader.Tell();
		// Skip the tag block to find where the optional Barrage body block lives.
		const int64 TagBytes = static_cast<int64>(Out.TagCount) * sizeof(uint16);
		Reader.Seek(Out.TagBlockOffset + TagBytes);

		Out.BarrageBodyOffset = Reader.Tell();
		if (Out.Flags & 0x01)
		{
			// Phase 5 Barrage body block = 88 bytes. Skip; Pass 3 reads it directly
			// from PayloadBytes via BarrageBodyOffset.
			Reader.Seek(Reader.Tell() + 88);
		}

		Out.ComponentBlockOffset = Reader.Tell();

		// Seek past component blocks so caller can read the NEXT entity header.
		Reader.Seek(Out.RecordStartOffset + Out.RecordSize);

		return true;
	}
}

FFlecsSaveSnapshotReader::FFlecsSaveSnapshotReader(TArray<uint8> InPayloadBytes)
	: PayloadBytes(MoveTemp(InPayloadBytes))
{
}

FFlecsSaveSnapshotReader::~FFlecsSaveSnapshotReader() = default;

bool FFlecsSaveSnapshotReader::CollectSavedSpawnerProvenance(TSet<TPair<FName, FName>>& OutTuples)
{
	OutTuples.Reset();

	constexpr int32 kMinPayloadBytes = sizeof(FFlecsSavePayloadHeader) + sizeof(FFlecsSavePayloadFooter);
	if (PayloadBytes.Num() < kMinPayloadBytes)
	{
		return false;
	}

	FMemoryReader Reader(PayloadBytes, /*bIsPersistent=*/ true);
	Reader.SetIsSaving(false);
	Reader.SetIsLoading(true);

	// Header — sanity check (don't apply anything; we just need EntityCount + path table).
	FFlecsSavePayloadHeader Header{};
	Reader.Serialize(&Header, sizeof(FFlecsSavePayloadHeader));
	if (Header.PayloadMagic != FatumSave::kPayloadMagic) return false;
	if (Header.PayloadVersion < 1 || Header.PayloadVersion > FatumSave::kPayloadVersion) return false;

	// Asset path table (we don't actually use the resolved definitions here, but the
	// reader's reader-side state needs to advance past the table for record offsets to
	// be correct).
	PathTable.Deserialize(Reader);

	// Walk each entity record and find any FSpawnerProvenance component block.
	const uint32 EntityCount = Header.EntityCount;
	for (uint32 i = 0; i < EntityCount; ++i)
	{
		const int64 RecordStart = Reader.Tell();

		// Per-entity header layout matches WriteEntityRecord:
		uint32 RecordSize = 0;
		uint32 SaveIndex = 0;
		uint64 OriginalFlecsId = 0;
		uint8 Flags = 0, ComponentCount = 0, TagCount = 0, Pad = 0;
		uint32 PathTableIndex = 0;
		Reader << RecordSize;
		Reader << SaveIndex;
		Reader << OriginalFlecsId;
		Reader << Flags;
		Reader << ComponentCount;
		Reader << TagCount;
		Reader << Pad;
		Reader << PathTableIndex;

		// Skip tag block + optional Barrage body block.
		Reader.Seek(Reader.Tell() + static_cast<int64>(TagCount) * sizeof(uint16));
		if (Flags & 0x01)
		{
			Reader.Seek(Reader.Tell() + 88);
		}

		// Component blocks.
		for (uint32 c = 0; c < ComponentCount; ++c)
		{
			uint16 ComponentTypeId = 0;
			uint32 BlockSize = 0;
			Reader << ComponentTypeId;
			Reader << BlockSize;

			const int64 BlockStart = Reader.Tell();
			const int64 BlockEnd = BlockStart + static_cast<int64>(BlockSize);

			if (ComponentTypeId == FlecsSaveTypeIds::kTypeId_SpawnerProvenance)
			{
				// Inline decode (uint16 Version + two FNames).
				uint16 Version = 0;
				Reader << Version;
				if (Version == 1)
				{
					FName LevelPath, ActorName;
					Reader << LevelPath;
					Reader << ActorName;
					if (!LevelPath.IsNone() && !ActorName.IsNone())
					{
						OutTuples.Add({LevelPath, ActorName});
					}
				}
			}

			Reader.Seek(BlockEnd);
		}

		// Defensive: ensure we land on the next record header.
		Reader.Seek(RecordStart + static_cast<int64>(RecordSize));
	}

	return true;
}

bool FFlecsSaveSnapshotReader::ApplyToFlecsWorld(flecs::world* World)
{
	checkf(World, TEXT("FFlecsSaveSnapshotReader::ApplyToFlecsWorld: null world"));

	constexpr int32 kMinPayloadBytes = sizeof(FFlecsSavePayloadHeader) + sizeof(FFlecsSavePayloadFooter);
	if (PayloadBytes.Num() < kMinPayloadBytes)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("SnapshotReader: payload too small (%d bytes; need >= %d)"),
			PayloadBytes.Num(), kMinPayloadBytes);
		return false;
	}

	FMemoryReader Reader(PayloadBytes, /*bIsPersistent=*/ true);
	Reader.SetIsSaving(false);
	Reader.SetIsLoading(true);

	// ── HEADER ──────────────────────────────────────────────────────────────
	FFlecsSavePayloadHeader Header{};
	Reader.Serialize(&Header, sizeof(FFlecsSavePayloadHeader));

	if (Header.PayloadMagic != FatumSave::kPayloadMagic)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("SnapshotReader: payload magic mismatch (got 0x%08X, expected 0x%08X)"),
			Header.PayloadMagic, FatumSave::kPayloadMagic);
		return false;
	}
	if (Header.PayloadVersion < 1 || Header.PayloadVersion > FatumSave::kPayloadVersion)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("SnapshotReader: unsupported payload version %u (max %u)"),
			Header.PayloadVersion, FatumSave::kPayloadVersion);
		return false;
	}
	if (Header.SpawnerDedupCount != 0)
	{
		// Phase 5 feature — Phase 2 readers don't support spawner dedup entries.
		UE_LOG(LogFlecsSave, Error,
			TEXT("SnapshotReader (Phase 2): payload contains %u spawner-dedup entries, not supported yet"),
			Header.SpawnerDedupCount);
		return false;
	}

	EntityCountInPayload = Header.EntityCount;
	SkippedMissingPrefabCount = 0;
	RemapTable.Reset();
	RemapTable.Reserve(static_cast<int32>(EntityCountInPayload));

	// ── ASSET PATH TABLE (immediately after payload header). ────────────────
	PathTable.Deserialize(Reader);

	// ── PASS 0: enumerate entity records, create entities, fill remap table. ──
	TArray<FRecordOffset> RecordOffsets;
	RecordOffsets.Reserve(static_cast<int32>(EntityCountInPayload));

	UFlecsArtillerySubsystem* Artillery = UFlecsArtillerySubsystem::SelfPtr;
	if (!Artillery)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("SnapshotReader: UFlecsArtillerySubsystem::SelfPtr is null — cannot resolve prefabs"));
		return false;
	}

	for (uint32 i = 0; i < EntityCountInPayload; ++i)
	{
		FRecordOffset Offset;
		if (!ReadEntityHeader(Reader, Offset))
		{
			UE_LOG(LogFlecsSave, Error,
				TEXT("SnapshotReader Pass 0: failed to read header for record %u"), i);
			return false;
		}
		checkf(Offset.SaveIndex == i,
			TEXT("SnapshotReader Pass 0: record %u has SaveIndex=%u (mismatch — corrupted payload)"),
			i, Offset.SaveIndex);

		UFlecsEntityDefinition* Definition = PathTable.ResolveDefinition(Offset.PathTableIndex);
		if (!Definition)
		{
			// v1 §7 line 1160-1163: missing prefab → log + skip entity, OldSaveIndex
			// maps to 0 sentinel so later cross-entity refs resolve cleanly.
			++SkippedMissingPrefabCount;
			RemapTable.Add(Offset.SaveIndex, static_cast<flecs::entity_t>(0));
			RecordOffsets.Add(Offset);
			UE_LOG(LogFlecsSave, Warning,
				TEXT("SnapshotReader Pass 0: SaveIndex=%u prefab asset (index %u) missing — entity skipped"),
				Offset.SaveIndex, Offset.PathTableIndex);
			continue;
		}

		flecs::entity Prefab = Artillery->GetOrCreateEntityPrefab(Definition);
		if (!Prefab.is_valid())
		{
			++SkippedMissingPrefabCount;
			RemapTable.Add(Offset.SaveIndex, static_cast<flecs::entity_t>(0));
			RecordOffsets.Add(Offset);
			UE_LOG(LogFlecsSave, Error,
				TEXT("SnapshotReader Pass 0: SaveIndex=%u prefab creation failed for definition '%s'"),
				Offset.SaveIndex, *Definition->GetPathName());
			continue;
		}

		flecs::entity NewEntity = World->entity().is_a(Prefab);
		RemapTable.Add(Offset.SaveIndex, NewEntity.id());
		RecordOffsets.Add(Offset);
	}

	checkf(RecordOffsets.Num() == static_cast<int32>(EntityCountInPayload),
		TEXT("SnapshotReader Pass 0: collected %d offsets but expected %u"),
		RecordOffsets.Num(), EntityCountInPayload);

	// ── PASS 1: tags + component decoders. Remap table is set for the duration. ──
	const FFlecsSaveComponentRegistry& Registry = FFlecsSaveComponentRegistry::Get();
	FlecsSaveRemap::GRemapTable = &RemapTable;
	ON_SCOPE_EXIT { FlecsSaveRemap::GRemapTable = nullptr; };

	// Decoders may need to resolve UFlecsEntityDefinition* references through the
	// path table (e.g. Decode_SmelterInstance rebuilds the ConsumedLedger entries).
	// Same TLS pattern as the writer side; checkNoEntry'd in encoder helpers below.
	FFlecsSaveAssetPathTable* PrevPathTable = FlecsSaveRemap::GPathTable;
	FlecsSaveRemap::GPathTable = &PathTable;
	ON_SCOPE_EXIT { FlecsSaveRemap::GPathTable = PrevPathTable; };

	int32 SuccessfulDecodes = 0;

	for (const FRecordOffset& Offset : RecordOffsets)
	{
		const flecs::entity_t* RemappedId = RemapTable.Find(Offset.SaveIndex);
		check(RemappedId);
		if (*RemappedId == 0)
		{
			// Entity was skipped in Pass 0 (missing prefab).
			continue;
		}

		flecs::entity Entity(*World, *RemappedId);
		if (!Entity.is_alive())
		{
			UE_LOG(LogFlecsSave, Error,
				TEXT("SnapshotReader Pass 1: SaveIndex=%u entity not alive after Pass 0"),
				Offset.SaveIndex);
			continue;
		}

		// ── Tags ────────────────────────────────────────────────────────────
		Reader.Seek(Offset.TagBlockOffset);
		for (uint32 t = 0; t < Offset.TagCount; ++t)
		{
			uint16 TagTypeId = 0;
			Reader << TagTypeId;
			const FlecsSave::FComponentDesc* Desc = Registry.FindByTypeId(TagTypeId);
			if (!Desc || !Desc->bIsTag)
			{
				UE_LOG(LogFlecsSave, Warning,
					TEXT("SnapshotReader Pass 1: SaveIndex=%u tag TypeId 0x%04X unknown — skipping"),
					Offset.SaveIndex, TagTypeId);
				continue;
			}
			// Add tag by name lookup — entity.add<T>() needs the type; the registry
			// holds only the debug name. Resolve the corresponding Flecs component
			// entity via world.lookup, then entity.add(componentEntity).
			const flecs::entity TagTypeEntity = World->lookup(TCHAR_TO_ANSI(Desc->DebugName));
			if (!TagTypeEntity.is_valid())
			{
				UE_LOG(LogFlecsSave, Error,
					TEXT("SnapshotReader Pass 1: tag type '%s' (TypeId 0x%04X) not found in Flecs world"),
					Desc->DebugName, TagTypeId);
				continue;
			}
			Entity.add(TagTypeEntity);
		}

		// ── Component blocks ────────────────────────────────────────────────
		Reader.Seek(Offset.ComponentBlockOffset);
		for (uint32 c = 0; c < Offset.ComponentCount; ++c)
		{
			uint16 ComponentTypeId = 0;
			uint32 BlockSize = 0;
			Reader << ComponentTypeId;
			Reader << BlockSize;

			const int64 BlockStart = Reader.Tell();
			const int64 BlockEnd = BlockStart + static_cast<int64>(BlockSize);

			const FlecsSave::FComponentDesc* Desc = Registry.FindByTypeId(ComponentTypeId);
			if (!Desc)
			{
				// Forward-compat: unknown component, skip bytes.
				UE_LOG(LogFlecsSave, Warning,
					TEXT("SnapshotReader Pass 1: SaveIndex=%u unknown component TypeId 0x%04X (size %u) — skipping"),
					Offset.SaveIndex, ComponentTypeId, BlockSize);
				Reader.Seek(BlockEnd);
				continue;
			}
			if (Desc->bIsTag)
			{
				UE_LOG(LogFlecsSave, Error,
					TEXT("SnapshotReader Pass 1: SaveIndex=%u component TypeId 0x%04X ('%s') registered as tag — payload corrupt"),
					Offset.SaveIndex, ComponentTypeId, Desc->DebugName);
				Reader.Seek(BlockEnd);
				continue;
			}
			checkf(Desc->Decoder != nullptr,
				TEXT("SnapshotReader Pass 1: TypeId 0x%04X ('%s') has no decoder"),
				ComponentTypeId, Desc->DebugName);

			// Decoder takes raw bytes — pass the buffer slice directly.
			const uint8* BlockData = &PayloadBytes[static_cast<int32>(BlockStart)];
			const bool bOk = Desc->Decoder(Entity, BlockData, BlockSize);
			if (!bOk)
			{
				UE_LOG(LogFlecsSave, Warning,
					TEXT("SnapshotReader Pass 1: SaveIndex=%u decoder for '%s' (TypeId 0x%04X) returned false"),
					Offset.SaveIndex, Desc->DebugName, ComponentTypeId);
			}

			Reader.Seek(BlockEnd);
		}

		++SuccessfulDecodes;
	}

	// ── PASS 3: Barrage body restore (Phase 5). ─────────────────────────────
	// For each record with Flags bit 0 set, read the 88-byte FBarrageBodyState block
	// directly from PayloadBytes at BarrageBodyOffset and recreate the Jolt body via
	// FlecsSaveBarrage::RestoreBarrageBodyForEntity. The helper handles binding the
	// entity via UFlecsArtillerySubsystem::BindEntityToBarrage on success.
	//
	// Sim-thread only (this entire ApplyToFlecsWorld lambda runs on the sim thread
	// per FlecsSaveSubsystem::DeferredLoadTick).
	int32 RestoredBodies = 0;
	int32 SkippedBodies = 0;
	if (UBarrageDispatch* Barrage = Artillery->GetBarrageDispatch())
	{
		for (const FRecordOffset& Offset : RecordOffsets)
		{
			if ((Offset.Flags & 0x01) == 0)
			{
				continue;  // entity had no body at save time
			}

			const flecs::entity_t* RemappedId = RemapTable.Find(Offset.SaveIndex);
			if (!RemappedId || *RemappedId == 0)
			{
				continue;  // entity skipped in Pass 0
			}

			flecs::entity Entity(*World, *RemappedId);
			if (!Entity.is_alive())
			{
				continue;
			}

			// Read the 88-byte body block from the payload.
			FBarrageBodyState BodyState;
			static_assert(sizeof(FBarrageBodyState) == 88,
				"SnapshotReader Pass 3 assumes 88-byte FBarrageBodyState block");
			if (Offset.BarrageBodyOffset + static_cast<int64>(sizeof(FBarrageBodyState)) > PayloadBytes.Num())
			{
				UE_LOG(LogFlecsSave, Error,
					TEXT("SnapshotReader Pass 3: SaveIndex=%u Barrage block extends past payload end"),
					Offset.SaveIndex);
				++SkippedBodies;
				continue;
			}
			FMemory::Memcpy(&BodyState, &PayloadBytes[static_cast<int32>(Offset.BarrageBodyOffset)],
				sizeof(FBarrageBodyState));

			if (FlecsSaveBarrage::RestoreBarrageBodyForEntity(Entity, BodyState, Barrage))
			{
				++RestoredBodies;
			}
			else
			{
				++SkippedBodies;
			}
		}
	}
	UE_LOG(LogFlecsSave, Log,
		TEXT("SnapshotReader Pass 3: restored %d Barrage bodies (%d skipped — see Warnings)"),
		RestoredBodies, SkippedBodies);

	// ── FOOTER (immediately after last entity record). ──────────────────────
	// Seek to expected footer position (right after the last record). The footer
	// magic + entity count echo provide our final sanity check.
	if (RecordOffsets.Num() > 0)
	{
		const FRecordOffset& Last = RecordOffsets.Last();
		Reader.Seek(Last.RecordStartOffset + Last.RecordSize);
	}

	FFlecsSavePayloadFooter Footer{};
	Reader.Serialize(&Footer, sizeof(FFlecsSavePayloadFooter));

	if (Footer.FooterMagic != FatumSave::kFooterMagic)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("SnapshotReader: footer magic mismatch (got 0x%08X, expected 0x%08X)"),
			Footer.FooterMagic, FatumSave::kFooterMagic);
		return false;
	}
	if (Footer.EntityCountEcho != Header.EntityCount)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("SnapshotReader: footer entity-count echo mismatch (header=%u, footer=%u)"),
			Header.EntityCount, Footer.EntityCountEcho);
		return false;
	}

	UE_LOG(LogFlecsSave, Log,
		TEXT("SnapshotReader: applied %d/%u entities (%d skipped due to missing prefab)"),
		SuccessfulDecodes, EntityCountInPayload, SkippedMissingPrefabCount);

	return true;
}
