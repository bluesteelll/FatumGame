// FlecsSaveSnapshotWriter — Phase 2 implementation. Walker + path table + entity blocks.

#include "FlecsSaveSnapshotWriter.h"

#include "FlecsSaveComponentRegistry.h"
#include "FlecsSaveFileFormat.h"
#include "FlecsSaveLog.h"

#include "Serialization/MemoryWriter.h"

#include "flecs.h"

namespace
{
	// Helper: serialize a const value through the archive without `const_cast` UB.
	template <typename T>
	FORCEINLINE void SaveValue(FArchive& Ar, T Value)
	{
		T Tmp = Value;
		Ar << Tmp;
	}

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
		//   bit0 = bHasBarrageBody (Phase 5; always 0 in Phase 2)
		//   bit1 = bHasSpawnerGuid (Phase 5; always 0 in Phase 2)
		const uint8 Flags = 0;
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

		// (Phase 2: no spawner GUID block, no Barrage body block.)

		// Tag IDs.
		for (uint16 TagId : Record.TagIds)
		{
			uint16 Tmp = TagId;
			Writer << Tmp;
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

void FFlecsSaveSnapshotWriter::WalkAndSerialize(flecs::world* World)
{
	checkf(World, TEXT("FFlecsSaveSnapshotWriter::WalkAndSerialize: null world"));

	SerializedBytes.Reset();
	EntityRecords.Reset();
	EntityCountWritten = 0;

	// ── PASS 1: walk entities (no archive yet — path table populates during this). ──
	Walker.Walk(*World, PathTable, EntityRecords);
	EntityCountWritten = static_cast<uint32>(EntityRecords.Num());

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
		TEXT("SnapshotWriter: wrote %d bytes (entities=%u, paths=%d)"),
		SerializedBytes.Num(), EntityCountWritten, PathTable.Num());

	// Discard records — only the byte buffer is consumed by the caller.
	EntityRecords.Reset();
}
