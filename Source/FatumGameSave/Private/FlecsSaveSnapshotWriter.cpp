// FlecsSaveSnapshotWriter — Phase 2 implementation. Walker + path table + entity blocks.

#include "FlecsSaveSnapshotWriter.h"

#include "FlecsSaveComponentRegistry.h"
#include "FlecsSaveFileFormat.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveRemap.h"

#include "Misc/ScopeExit.h"
#include "Serialization/MemoryWriter.h"

#include "Encoders/FlecsSaveEncoderHelpers.h"   // SaveValue template — moved out of anonymous ns to fix unity-build collision

#include "flecs.h"

using FlecsSaveEnc::SaveValue;

namespace
{
	/** Per-entity header layout per v1 §2 line 138-180. EntityRecordSize is back-patched
	 *  after the record body is written so callers can skip-forward over unknown entities. */
	void WriteEntityRecord(
		FMemoryWriter& Writer,
		TArray<uint8>& Bytes,
		const FEntityRecord& Record)
	{
		const int64 RecordStartOffset = Writer.Tell();

		// Placeholder for EntityRecordSize — back-patch at end of record.
		uint32 RecordSizePlaceholder = 0;
		Writer << RecordSizePlaceholder;

		// SaveIndex + OriginalFlecsId (diagnostic only).
		SaveValue<uint32>(Writer, Record.SaveIndex);
		SaveValue<uint64>(Writer, static_cast<uint64>(Record.OriginalId));

		// Flags + counts + pad + prefab path hash.
		//   bit 0 = bHasBarrageBody  (Phase 5 — emits 88-byte body block after tag block)
		//   bit 1 = bHasSpawnerGuid  (RETIRED v2 §C9 — never set; reserved for future use)
		uint8 Flags = 0;
		if (Record.bHasBarrageBody)
		{
			Flags |= 0x01;
		}
		SaveValue<uint8>(Writer, Flags);

		const int32 NumComponents = Record.Components.Num();
		const int32 NumTags = Record.TagIds.Num();
		checkf(NumComponents <= 255,
			TEXT("WriteEntityRecord: entity %llu has %d components (> uint8 max)"),
			static_cast<uint64>(Record.OriginalId), NumComponents);
		checkf(NumTags <= 255,
			TEXT("WriteEntityRecord: entity %llu has %d tags (> uint8 max)"),
			static_cast<uint64>(Record.OriginalId), NumTags);
		SaveValue<uint8>(Writer, static_cast<uint8>(NumComponents));
		SaveValue<uint8>(Writer, static_cast<uint8>(NumTags));
		SaveValue<uint8>(Writer, 0u); // _pad

		// PathTableIndex is the canonical key (was "PrefabPathHash" in v1; the field
		// is the path-table dedup index per v2 §M11 since we own a string table). The
		// reader looks up the asset via FFlecsSaveAssetPathTable::ResolveDefinition.
		SaveValue<uint32>(Writer, Record.PathTableIndex);

		// Tag IDs (always BEFORE the Barrage body block — reader matches this order).
		for (uint16 TagId : Record.TagIds)
		{
			uint16 Tmp = TagId;
			Writer << Tmp;
		}

		// Phase 5 — Barrage body block (88 bytes). Emitted between tag block and
		// component block when bit 0 of Flags is set. Reader's offset table skips this
		// block by the same amount when the bit is clear.
		if (Record.bHasBarrageBody)
		{
			static_assert(sizeof(FBarrageBodyState) == 88,
				"WriteEntityRecord assumes 88-byte FBarrageBodyState");
			Writer.Serialize(const_cast<FBarrageBodyState*>(&Record.BarrageBody),
				sizeof(FBarrageBodyState));
		}

		// Component blocks.
		for (const FEntityRecord::FComponentBlock& Block : Record.Components)
		{
			SaveValue<uint16>(Writer, Block.TypeId);
			SaveValue<uint32>(Writer, static_cast<uint32>(Block.Bytes.Num()));
			if (Block.Bytes.Num() > 0)
			{
				Writer.Serialize(const_cast<uint8*>(Block.Bytes.GetData()), Block.Bytes.Num());
			}
		}

		// Back-patch EntityRecordSize.
		const int64 RecordEndOffset = Writer.Tell();
		const uint32 RecordSize = static_cast<uint32>(RecordEndOffset - RecordStartOffset);
		uint32* SizeSlot = reinterpret_cast<uint32*>(&Bytes[static_cast<int32>(RecordStartOffset)]);
		*SizeSlot = RecordSize;
	}
}

FFlecsSaveSnapshotWriter::FFlecsSaveSnapshotWriter()
{
	// Reserve a small initial buffer; the array grows as records are encoded.
	// TODO(Phase 7 m6): make the reserve size INI-driven.
	SerializedBytes.Reserve(64 * 1024);
}

FFlecsSaveSnapshotWriter::~FFlecsSaveSnapshotWriter() = default;

void FFlecsSaveSnapshotWriter::WalkAndSerialize(flecs::world* World, const FString& WorldName)
{
	checkf(World, TEXT("FFlecsSaveSnapshotWriter::WalkAndSerialize: null world"));

	SerializedBytes.Reset();
	EntityRecords.Reset();
	EntityCountWritten = 0;

	// Set the active path-table TLS pointer for the duration of this save operation
	// so encoders (e.g. Encode_SmelterInstance) can register ingredient asset paths
	// via GPathTable->RegisterPath. Cleared on scope exit — every TLS-using API
	// (RegisterPath / ResolveDefinition) checkNoEntry if accessed outside this window.
	FFlecsSaveAssetPathTable* PrevPathTable = FlecsSaveRemap::GPathTable;
	FlecsSaveRemap::GPathTable = &PathTable;
	ON_SCOPE_EXIT { FlecsSaveRemap::GPathTable = PrevPathTable; };

	// ── PASS 0: pre-walk to collect candidate entity ids (sorted entity_t ascending)
	// and build the reverse map BEFORE encoders run. Encoders called during Walk()
	// may reference cross-entity ids via FlecsSaveRemap::EntityToSaveIndex; without
	// GReverseMap pre-populated, every such call would hit checkNoEntry().
	//
	// PreWalkCollectIds is read-only (no encoder dispatch, no path-table registration).
	// SaveIndex == position in the sorted id array — Walker.Walk() uses identical sort
	// rules and asserts the count agrees post-encode.
	{
		TArray<flecs::entity_t> SortedIds;
		Walker.PreWalkCollectIds(*World, SortedIds);
		ReverseMapStorage.Reset();
		ReverseMapStorage.Reserve(SortedIds.Num());
		for (int32 i = 0; i < SortedIds.Num(); ++i)
		{
			ReverseMapStorage.Add(SortedIds[i], static_cast<uint32>(i));
		}
	}
	const TMap<flecs::entity_t, uint32>* PrevReverseMap = FlecsSaveRemap::GReverseMap;
	FlecsSaveRemap::GReverseMap = &ReverseMapStorage;
	ON_SCOPE_EXIT { FlecsSaveRemap::GReverseMap = PrevReverseMap; };

	// ── PASS 1: walk entities (path table populates during this; encoders may now
	//            call EntityToSaveIndex since GReverseMap is set above). ──
	Walker.Walk(*World, PathTable, EntityRecords);
	EntityCountWritten = static_cast<uint32>(EntityRecords.Num());

	// Sanity-check: the walker's SaveIndex assignments must match the pre-walk's.
	// If they diverge the world mutated between passes (snapshot fence violated).
	checkf(EntityCountWritten == static_cast<uint32>(ReverseMapStorage.Num()),
		TEXT("SnapshotWriter: pre-walk produced %d ids but main walk produced %u — "
		     "world mutated between passes (snapshot guard violated)"),
		ReverseMapStorage.Num(), EntityCountWritten);

	// ── PASS 2: serialize into the byte buffer. ────────────────────────────────────
	FMemoryWriter Writer(SerializedBytes, /*bIsPersistent=*/ true);
	Writer.SetIsSaving(true);
	Writer.SetIsLoading(false);

	// Payload header.
	FFlecsSavePayloadHeader Header{};
	Header.PayloadMagic      = FatumSave::kPayloadMagic;
	Header.PayloadVersion    = FatumSave::kPayloadVersion;
	Header.EntityCount       = EntityCountWritten;
	Header.SpawnerDedupCount = 0;  // Phase 5
	Writer.Serialize(&Header, sizeof(FFlecsSavePayloadHeader));

	// Phase 7 — WorldName (length-prefixed UTF-8 + pad-to-4) immediately after the
	// payload header. Reader compares this against the loading world's map name and
	// returns ELoadResult::WorldMismatch on mismatch (cross-level loads out of scope
	// per Q10). Same length-prefix + UTF-8 + pad format as FFlecsSaveAssetPathTable
	// entries for symmetry and 4-byte alignment of subsequent uint32 reads.
	{
		const FTCHARToUTF8 Utf8(*WorldName);
		uint32 ByteLen = static_cast<uint32>(Utf8.Length());
		Writer << ByteLen;
		if (ByteLen > 0)
		{
			Writer.Serialize(
				const_cast<ANSICHAR*>(reinterpret_cast<const ANSICHAR*>(Utf8.Get())),
				ByteLen);
		}
		const uint32 Pad = (4u - (ByteLen & 3u)) & 3u;
		for (uint32 i = 0; i < Pad; ++i)
		{
			uint8 Zero = 0;
			Writer << Zero;
		}
	}

	// Asset path table — MUST come before entity records so the reader can resolve
	// PathTableIndex during entity creation in Pass 0.
	PathTable.Serialize(Writer);

	// Entity records (sorted by entity id ascending in Walker).
	for (const FEntityRecord& Record : EntityRecords)
	{
		WriteEntityRecord(Writer, SerializedBytes, Record);
	}

	// Payload footer.
	FFlecsSavePayloadFooter Footer{};
	Footer.FooterMagic     = FatumSave::kFooterMagic;
	Footer.EntityCountEcho = EntityCountWritten;
	Writer.Serialize(&Footer, sizeof(FFlecsSavePayloadFooter));

	UE_LOG(LogFlecsSave, Log,
		TEXT("SnapshotWriter: wrote %d bytes (entities=%u, paths=%d, world='%s')"),
		SerializedBytes.Num(), EntityCountWritten, PathTable.Num(), *WorldName);

	// Discard records — only the byte buffer is consumed by the caller.
	EntityRecords.Reset();
}
