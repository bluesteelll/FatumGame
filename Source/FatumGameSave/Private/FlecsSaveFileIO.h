// FlecsSaveFileIO — low-level file I/O for save/load. PRIVATE header (consumers are
// inside the FatumGameSave module only).
//
// Responsibilities:
//   - Compose final on-disk byte layout (header + compressed blob) — write side.
//   - Atomic-rename via OpenWrite + IFileHandle::Flush(/*bFullFlush=*/true) + Move (per v2 §M9).
//   - Rolling backup rotation: .bak3 ← .bak2 ← .bak1 ← .sav.
//   - Read+verify path: load file, validate header (magic + version), CRC-check
//     compressed blob, decompress, fall back through backups on failure.
//   - CRC32 helper (forwards to FCrc::MemCrc32 with the standard seed).

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "FlecsSaveTypes.h"

namespace FlecsSaveIO
{
	/** Compress an uncompressed payload using Oodle Mermaid with BiasSpeed (per v2 §M12).
	 *  Returns true on success. OutCompressed is reset before writing. */
	FATUMGAMESAVE_API bool CompressPayload(const TArray<uint8>& Uncompressed, TArray<uint8>& OutCompressed);

	/** Decompress a previously-compressed payload. Caller supplies the original uncompressed size
	 *  from the file header. Returns true on success; OutUncompressed.Num() == UncompressedSize on success. */
	FATUMGAMESAVE_API bool DecompressPayload(const TArray<uint8>& Compressed, int32 UncompressedSize, TArray<uint8>& OutUncompressed);

	/** Compute CRC32 over the bytes using the standard Unreal seed. */
	FATUMGAMESAVE_API uint32 ComputeCrc32(const TArray<uint8>& Bytes);

	/** Build the absolute path for a slot file at the given backup generation.
	 *  Gen == 0 → "<Project>/SaveGames/Slot_XX.sav"
	 *  Gen == 1 → "<Project>/SaveGames/Slot_XX.sav.bak1"
	 *  Gen == N → "<Project>/SaveGames/Slot_XX.sav.bakN"
	 *  SlotName must be "Slot_XX" (zero-padded), enforced by caller. */
	FATUMGAMESAVE_API FString GetSlotFilePath(const FString& SlotName, int32 BackupGen);

	/** Ensure the SaveGames directory exists. Returns false on filesystem error. */
	FATUMGAMESAVE_API bool EnsureSaveDirectoryExists();

	/** Number of rolling backups maintained per slot. */
	inline constexpr int32 kBackupChainDepth = 3;

	/** Save game build hash for the running build. Phase 1 returns a placeholder. */
	FATUMGAMESAVE_API uint64 GetGameBuildHash();

	/** Rotate the backup chain BEFORE writing a new save. After rotation:
	 *   .bak2 (old) → .bak3 (overwriting), .bak1 (old) → .bak2, .sav (old) → .bak1.
	 *  The slot's main .sav file is moved out, freeing the slot for the fresh write.
	 *  Missing intermediate files are tolerated. */
	FATUMGAMESAVE_API void RotateBackupChain(const FString& SlotName);

	/** Compose final file bytes (header + compressed payload), then atomic-write:
	 *    1. OpenWrite to "<final>.tmp"
	 *    2. Write header + compressed
	 *    3. IFileHandle::Flush(bFullFlush=true) — real fsync (v2 M9)
	 *    4. Close handle
	 *    5. IFileManager::Move(final, tmp, Replace=true)
	 *  Returns ESaveResult::Success / IOFailed. */
	FATUMGAMESAVE_API ESaveResult WriteSaveFile(const FString& SlotName, const TArray<uint8>& Compressed, uint32 UncompressedSize);

	/** Read + verify pipeline for one slot file path (used for main slot and each backup):
	 *    1. Read header bytes; bail on size/IO mismatch.
	 *    2. Validate magic + version range.
	 *    3. Read compressed payload (PayloadSizeBytes).
	 *    4. Compute CRC32; compare to header.
	 *    5. Decompress to UncompressedSize.
	 *  OutUncompressed is populated only on Success. */
	FATUMGAMESAVE_API ELoadResult VerifyAndDecompress(const FString& FilePath, TArray<uint8>& OutUncompressed);

	/** Read the main slot file; on any failure walk the backup chain in order .bak1 → .bak2 → .bak3.
	 *  Returns the result code of the first file that loads successfully, or AllBackupsFailed equivalent
	 *  (we use CrcMismatch) when none works. */
	FATUMGAMESAVE_API ELoadResult ReadSaveFileWithBackups(const FString& SlotName, TArray<uint8>& OutUncompressed);
}
