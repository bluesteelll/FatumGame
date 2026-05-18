// FlecsSaveSnapshotWriter — Phase 1 stub. Writes only header + footer (0 entities).

#include "FlecsSaveSnapshotWriter.h"
#include "FlecsSaveFileFormat.h"
#include "FlecsSaveLog.h"
#include "Serialization/MemoryWriter.h"

FFlecsSaveSnapshotWriter::FFlecsSaveSnapshotWriter()
{
	// Reserve enough to avoid a realloc for the Phase 1 stub payload (24 bytes total).
	// Phase 2 will grow this dramatically and may pull the reserve from INI (m6).
	SerializedBytes.Reserve(64);
}

FFlecsSaveSnapshotWriter::~FFlecsSaveSnapshotWriter()
{
	// Nothing to do — TArray clears itself.
}

void FFlecsSaveSnapshotWriter::WalkAndSerialize(flecs::world* /*World*/)
{
	// PHASE 1: do not touch the world. Emit a minimal valid payload so the rest of the
	// pipeline (compress, CRC, write, fsync, rename, rotate, read, verify, decompress,
	// reader-accept) can be smoke-tested end-to-end without entity walking.

	SerializedBytes.Reset();

	FMemoryWriter Writer(SerializedBytes, /*bIsPersistent=*/ true);
	Writer.SetIsSaving(true);
	Writer.SetIsLoading(false);

	// ── Payload header ────────────────────────────────────────────────────
	FFlecsSavePayloadHeader Header{};
	Header.PayloadMagic      = FatumSave::kPayloadMagic;
	Header.PayloadVersion    = FatumSave::kPayloadVersion;
	Header.EntityCount       = 0;
	Header.SpawnerDedupCount = 0;
	Writer.Serialize(&Header, sizeof(FFlecsSavePayloadHeader));

	// ── (No entities in Phase 1) ──────────────────────────────────────────

	// ── Payload footer ────────────────────────────────────────────────────
	FFlecsSavePayloadFooter Footer{};
	Footer.FooterMagic     = FatumSave::kFooterMagic;
	Footer.EntityCountEcho = 0;
	Writer.Serialize(&Footer, sizeof(FFlecsSavePayloadFooter));

	EntityCountWritten = 0;

	UE_LOG(LogFlecsSave, Verbose,
		TEXT("SnapshotWriter (Phase 1 stub): wrote %d bytes (header+footer only, 0 entities)"),
		SerializedBytes.Num());
}
