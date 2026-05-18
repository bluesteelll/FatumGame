// FlecsSaveSnapshotReader — Phase 1 stub. Validates header/footer and accepts 0-entity payload.

#include "FlecsSaveSnapshotReader.h"
#include "FlecsSaveFileFormat.h"
#include "FlecsSaveLog.h"
#include "Serialization/MemoryReader.h"

FFlecsSaveSnapshotReader::FFlecsSaveSnapshotReader(TArray<uint8> InPayloadBytes)
	: PayloadBytes(MoveTemp(InPayloadBytes))
{
}

FFlecsSaveSnapshotReader::~FFlecsSaveSnapshotReader()
{
	// Nothing to do — TArray clears itself.
}

bool FFlecsSaveSnapshotReader::ApplyToFlecsWorld(flecs::world* /*World*/)
{
	// PHASE 1: validate the framing, do nothing else.
	// The 24-byte minimum (header 16 + footer 8) is the Phase 1 stub payload size.
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

	// ── Header ────────────────────────────────────────────────────────────
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

	EntityCountInPayload = Header.EntityCount;

	// PHASE 1: we cannot decode any entities. If the writer emitted any, that's a bug
	// (since the writer is also a Phase 1 stub). Fail-fast — silent acceptance would
	// hide write-side regressions.
	if (Header.EntityCount != 0)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("SnapshotReader (Phase 1 stub): payload contains %u entities, but no decoders are registered yet"),
			Header.EntityCount);
		return false;
	}
	if (Header.SpawnerDedupCount != 0)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("SnapshotReader (Phase 1 stub): payload contains %u spawner-dedup entries, not supported yet"),
			Header.SpawnerDedupCount);
		return false;
	}

	// ── Footer (immediately after header in the 0-entity case) ────────────
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

	UE_LOG(LogFlecsSave, Verbose,
		TEXT("SnapshotReader (Phase 1 stub): accepted 0-entity payload (%d bytes)"),
		PayloadBytes.Num());
	return true;
}
