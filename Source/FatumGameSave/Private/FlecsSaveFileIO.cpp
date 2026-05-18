// FlecsSaveFileIO — file I/O, compression, CRC, atomic-rename, rolling backups.

#include "FlecsSaveFileIO.h"
#include "FlecsSaveFileFormat.h"
#include "FlecsSaveLog.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"
#include "Misc/Compression.h"
#include "Misc/CompressionFlags.h"

namespace FlecsSaveIO
{
	// ═══════════════════════════════════════════════════════════════
	// PATHS
	// ═══════════════════════════════════════════════════════════════

	FString GetSlotFilePath(const FString& SlotName, int32 BackupGen)
	{
		checkf(BackupGen >= 0 && BackupGen <= kBackupChainDepth,
			TEXT("GetSlotFilePath: BackupGen out of range (%d, allowed [0, %d])"),
			BackupGen, kBackupChainDepth);

		FString Base = FPaths::ProjectSavedDir() / TEXT("SaveGames") / SlotName + TEXT(".sav");
		if (BackupGen == 0)
		{
			return Base;
		}
		return Base + FString::Printf(TEXT(".bak%d"), BackupGen);
	}

	bool EnsureSaveDirectoryExists()
	{
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("SaveGames");
		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
		if (PF.DirectoryExists(*Dir))
		{
			return true;
		}
		if (!PF.CreateDirectoryTree(*Dir))
		{
			UE_LOG(LogFlecsSave, Error, TEXT("EnsureSaveDirectoryExists: failed to create '%s'"), *Dir);
			return false;
		}
		return true;
	}

	uint64 GetGameBuildHash()
	{
		// Phase 1: encode just the engine major/minor. Phase 2+ will fold in the
		// build configuration hash (git SHA, build flags, etc.) so corrupted saves
		// from another build refuse to load.
		constexpr uint64 EngineMajor = 5ull;
		constexpr uint64 EngineMinor = 7ull;
		return (EngineMajor << 32) | EngineMinor;
	}

	// ═══════════════════════════════════════════════════════════════
	// CRC
	// ═══════════════════════════════════════════════════════════════

	uint32 ComputeCrc32(const TArray<uint8>& Bytes)
	{
		// Standard Unreal CRC32 over the raw byte range. Empty arrays return the seed —
		// our caller never CRCs an empty payload (we always have at least the stub 24B).
		return FCrc::MemCrc32(Bytes.GetData(), Bytes.Num());
	}

	// ═══════════════════════════════════════════════════════════════
	// COMPRESSION (Oodle Mermaid + BiasSpeed)
	// ═══════════════════════════════════════════════════════════════

	bool CompressPayload(const TArray<uint8>& Uncompressed, TArray<uint8>& OutCompressed)
	{
		OutCompressed.Reset();
		if (Uncompressed.Num() == 0)
		{
			UE_LOG(LogFlecsSave, Error, TEXT("CompressPayload: empty input"));
			return false;
		}

		// Worst-case bound + small safety margin so the compressor never reports
		// "destination buffer too small" for highly random data.
		int32 MaxCompressedSize = FCompression::CompressMemoryBound(
			NAME_Oodle,
			Uncompressed.Num(),
			COMPRESS_BiasSpeed);

		OutCompressed.SetNumUninitialized(MaxCompressedSize);
		int32 ActualSize = MaxCompressedSize;

		const bool bOk = FCompression::CompressMemory(
			NAME_Oodle,
			OutCompressed.GetData(), ActualSize,
			Uncompressed.GetData(), Uncompressed.Num(),
			COMPRESS_BiasSpeed);

		if (!bOk)
		{
			UE_LOG(LogFlecsSave, Error, TEXT("CompressPayload: Oodle compress failed"));
			OutCompressed.Reset();
			return false;
		}

		// Trim down to the actual compressed size.
		OutCompressed.SetNum(ActualSize, EAllowShrinking::No);
		return true;
	}

	bool DecompressPayload(const TArray<uint8>& Compressed, int32 UncompressedSize, TArray<uint8>& OutUncompressed)
	{
		OutUncompressed.Reset();
		if (Compressed.Num() == 0 || UncompressedSize <= 0)
		{
			UE_LOG(LogFlecsSave, Error,
				TEXT("DecompressPayload: bad inputs (compressed=%d, uncompressed=%d)"),
				Compressed.Num(), UncompressedSize);
			return false;
		}

		OutUncompressed.SetNumUninitialized(UncompressedSize);

		const bool bOk = FCompression::UncompressMemory(
			NAME_Oodle,
			OutUncompressed.GetData(), UncompressedSize,
			Compressed.GetData(), Compressed.Num(),
			COMPRESS_BiasSpeed);

		if (!bOk)
		{
			UE_LOG(LogFlecsSave, Error, TEXT("DecompressPayload: Oodle decompress failed"));
			OutUncompressed.Reset();
			return false;
		}

		return true;
	}

	// ═══════════════════════════════════════════════════════════════
	// BACKUP ROTATION
	// ═══════════════════════════════════════════════════════════════

	void RotateBackupChain(const FString& SlotName)
	{
		IFileManager& FM = IFileManager::Get();

		// Walk from the oldest backup down. Anything missing is silently skipped — rotation
		// is best-effort; the goal is "after this call, the .sav slot is free to receive
		// a fresh write, and we did not destroy older history we still had on disk".
		//
		// Order matters: we overwrite from oldest forward so newer files survive.
		for (int32 Gen = kBackupChainDepth; Gen >= 1; --Gen)
		{
			const FString From = GetSlotFilePath(SlotName, Gen - 1);
			const FString To   = GetSlotFilePath(SlotName, Gen);
			if (FM.FileExists(*From))
			{
				if (!FM.Move(*To, *From, /*Replace=*/true, /*EvenIfReadOnly=*/false))
				{
					UE_LOG(LogFlecsSave, Warning,
						TEXT("RotateBackupChain: failed to move '%s' -> '%s' (continuing)"),
						*From, *To);
				}
			}
		}
	}

	// ═══════════════════════════════════════════════════════════════
	// ATOMIC WRITE (open .tmp → fsync → rename)
	// ═══════════════════════════════════════════════════════════════

	ESaveResult WriteSaveFile(const FString& SlotName, const TArray<uint8>& Compressed, uint32 UncompressedSize)
	{
		if (!EnsureSaveDirectoryExists())
		{
			return ESaveResult::IOFailed;
		}

		const FString FinalPath = GetSlotFilePath(SlotName, 0);
		const FString TempPath  = FinalPath + TEXT(".tmp");

		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();

		// Wipe any stale .tmp from a prior aborted save — non-fatal if it doesn't exist.
		if (PF.FileExists(*TempPath))
		{
			PF.DeleteFile(*TempPath);
		}

		FFlecsSaveHeader Header{};
		Header.MagicBytes       = FatumSave::kMagic;
		Header.Version          = FatumSave::kVersion;
		Header.PayloadSizeBytes = static_cast<uint32>(Compressed.Num());
		Header.GameBuildHash    = GetGameBuildHash();
		Header.CrcOfPayload     = ComputeCrc32(Compressed);
		Header.UncompressedSize = UncompressedSize;

		// Order is: write .tmp → fsync → rotate chain → rename .tmp → .sav.
		// Previous version rotated FIRST, which destroyed the prior main .sav before the
		// new bytes were proven good. If write/fsync failed after rotation, user lost
		// their last good save with nothing to fall back to. Now: any failure before the
		// final rename leaves the previous .sav untouched.
		IFileHandle* Handle = PF.OpenWrite(*TempPath, /*bAppend=*/ false, /*bAllowRead=*/ false);
		if (Handle == nullptr)
		{
			UE_LOG(LogFlecsSave, Error, TEXT("WriteSaveFile: OpenWrite failed for '%s'"), *TempPath);
			return ESaveResult::IOFailed;
		}

		bool bWriteOk = true;
		bWriteOk &= Handle->Write(reinterpret_cast<const uint8*>(&Header), sizeof(FFlecsSaveHeader));
		if (Compressed.Num() > 0)
		{
			bWriteOk &= Handle->Write(Compressed.GetData(), Compressed.Num());
		}

		if (!bWriteOk)
		{
			UE_LOG(LogFlecsSave, Error, TEXT("WriteSaveFile: Write failed for '%s'"), *TempPath);
			delete Handle;
			PF.DeleteFile(*TempPath);
			return ESaveResult::IOFailed;
		}

		// REAL fsync — pushes the kernel's page cache to disk. Slow on HDDs (~10ms),
		// negligible on NVMe. Must complete BEFORE rotation; otherwise a power loss
		// between rotate and fsync would corrupt the rotated backup chain with a
		// not-yet-on-disk new file.
		Handle->Flush(/*bFullFlush=*/ true);
		delete Handle;
		Handle = nullptr;

		// New bytes are durably on disk. Now rotate the backup chain — prior main .sav
		// becomes .bak1, .bak1 becomes .bak2, etc. This is best-effort; failure to rotate
		// is a warning, not a fatal — the new save still proceeds via the rename below.
		RotateBackupChain(SlotName);

		// Atomic rename. On Windows IFileManager::Move uses MoveFileEx with REPLACE_EXISTING
		// which is atomic for files on the same volume — exactly what we want for the
		// "old slot file → new slot file" swap.
		IFileManager& FM = IFileManager::Get();
		if (!FM.Move(*FinalPath, *TempPath, /*Replace=*/true, /*EvenIfReadOnly=*/false))
		{
			UE_LOG(LogFlecsSave, Error,
				TEXT("WriteSaveFile: atomic rename failed ('%s' -> '%s')"),
				*TempPath, *FinalPath);
			// Leave the .tmp on disk for postmortem — DON'T delete it; the user can
			// salvage it by hand. This is an exceptional path.
			return ESaveResult::IOFailed;
		}

		UE_LOG(LogFlecsSave, Log,
			TEXT("WriteSaveFile: '%s' written (%d bytes compressed, %u uncompressed, CRC=0x%08X)"),
			*FinalPath, Compressed.Num(), UncompressedSize, Header.CrcOfPayload);
		return ESaveResult::Success;
	}

	// ═══════════════════════════════════════════════════════════════
	// READ + VERIFY (single file)
	// ═══════════════════════════════════════════════════════════════

	ELoadResult VerifyAndDecompress(const FString& FilePath, TArray<uint8>& OutUncompressed)
	{
		OutUncompressed.Reset();

		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
		if (!PF.FileExists(*FilePath))
		{
			return ELoadResult::FileMissing;
		}

		TArray<uint8> FileBytes;
		if (!FFileHelper::LoadFileToArray(FileBytes, *FilePath))
		{
			UE_LOG(LogFlecsSave, Error, TEXT("VerifyAndDecompress: LoadFileToArray failed for '%s'"), *FilePath);
			return ELoadResult::UnknownError;
		}

		if (FileBytes.Num() < static_cast<int32>(sizeof(FFlecsSaveHeader)))
		{
			UE_LOG(LogFlecsSave, Error,
				TEXT("VerifyAndDecompress: '%s' too small (%d bytes; need >= %d)"),
				*FilePath, FileBytes.Num(), (int32)sizeof(FFlecsSaveHeader));
			return ELoadResult::MagicMismatch;  // treat as malformed
		}

		FFlecsSaveHeader Header{};
		FMemory::Memcpy(&Header, FileBytes.GetData(), sizeof(FFlecsSaveHeader));

		if (Header.MagicBytes != FatumSave::kMagic)
		{
			UE_LOG(LogFlecsSave, Error,
				TEXT("VerifyAndDecompress: '%s' magic mismatch (got 0x%08X, expected 0x%08X)"),
				*FilePath, Header.MagicBytes, FatumSave::kMagic);
			return ELoadResult::MagicMismatch;
		}
		if (Header.Version < FatumSave::kMinVersion || Header.Version > FatumSave::kVersion)
		{
			UE_LOG(LogFlecsSave, Error,
				TEXT("VerifyAndDecompress: '%s' unsupported version %u (allowed [%u, %u])"),
				*FilePath, Header.Version, FatumSave::kMinVersion, FatumSave::kVersion);
			return ELoadResult::VersionMismatch;
		}

		const int32 ExpectedTotal = static_cast<int32>(sizeof(FFlecsSaveHeader)) + static_cast<int32>(Header.PayloadSizeBytes);
		if (FileBytes.Num() < ExpectedTotal)
		{
			UE_LOG(LogFlecsSave, Error,
				TEXT("VerifyAndDecompress: '%s' truncated (header claims %u payload bytes, file has %d total)"),
				*FilePath, Header.PayloadSizeBytes, FileBytes.Num());
			return ELoadResult::CrcMismatch;  // closest match — payload not complete
		}

		// Extract compressed blob.
		TArray<uint8> Compressed;
		Compressed.SetNumUninitialized(Header.PayloadSizeBytes);
		if (Header.PayloadSizeBytes > 0)
		{
			FMemory::Memcpy(Compressed.GetData(),
				FileBytes.GetData() + sizeof(FFlecsSaveHeader),
				Header.PayloadSizeBytes);
		}

		// CRC check on the compressed bytes (before decompress, per v1 spec — fast-fail).
		const uint32 ActualCrc = ComputeCrc32(Compressed);
		if (ActualCrc != Header.CrcOfPayload)
		{
			UE_LOG(LogFlecsSave, Error,
				TEXT("VerifyAndDecompress: '%s' CRC mismatch (got 0x%08X, expected 0x%08X)"),
				*FilePath, ActualCrc, Header.CrcOfPayload);
			return ELoadResult::CrcMismatch;
		}

		if (!DecompressPayload(Compressed, static_cast<int32>(Header.UncompressedSize), OutUncompressed))
		{
			return ELoadResult::DecompressFail;
		}

		UE_LOG(LogFlecsSave, Log,
			TEXT("VerifyAndDecompress: '%s' OK (%d compressed, %u uncompressed, CRC=0x%08X)"),
			*FilePath, Compressed.Num(), Header.UncompressedSize, Header.CrcOfPayload);
		return ELoadResult::Success;
	}

	// ═══════════════════════════════════════════════════════════════
	// READ WITH BACKUP FALLBACK
	// ═══════════════════════════════════════════════════════════════

	ELoadResult ReadSaveFileWithBackups(const FString& SlotName, TArray<uint8>& OutUncompressed)
	{
		// Try main slot first.
		const FString MainPath = GetSlotFilePath(SlotName, 0);
		ELoadResult R = VerifyAndDecompress(MainPath, OutUncompressed);
		if (R == ELoadResult::Success)
		{
			return R;
		}

		if (R == ELoadResult::FileMissing)
		{
			// No main file at all — the user has never saved here.
			UE_LOG(LogFlecsSave, Log, TEXT("ReadSaveFileWithBackups: no save at slot '%s'"), *SlotName);
			return ELoadResult::FileMissing;
		}

		// Main file exists but failed validation (magic, version, CRC, decompress). Walk backups.
		// Phase 7 — track whether ANY backup file existed so we can return the
		// most-informative final code:
		//   - At least one backup existed but all failed → preserve the main failure code
		//     (CrcMismatch / VersionMismatch / DecompressFail — diagnostic-friendly)
		//   - No backups existed at all → AllBackupsFailed (signals "no recovery possible")
		UE_LOG(LogFlecsSave, Warning,
			TEXT("ReadSaveFileWithBackups: main slot '%s' failed (%d); walking backups"),
			*SlotName, (int32)R);

		bool bAnyBackupExisted = false;

		for (int32 Gen = 1; Gen <= kBackupChainDepth; ++Gen)
		{
			const FString BackupPath = GetSlotFilePath(SlotName, Gen);
			const ELoadResult BR = VerifyAndDecompress(BackupPath, OutUncompressed);
			if (BR == ELoadResult::Success)
			{
				UE_LOG(LogFlecsSave, Warning,
					TEXT("ReadSaveFileWithBackups: recovered from backup .bak%d"),
					Gen);
				return ELoadResult::Success;
			}
			if (BR != ELoadResult::FileMissing)
			{
				bAnyBackupExisted = true;
				UE_LOG(LogFlecsSave, Warning,
					TEXT("ReadSaveFileWithBackups: '%s' .bak%d also failed (%d)"),
					*SlotName, Gen, (int32)BR);
			}
		}

		if (!bAnyBackupExisted)
		{
			// Main was corrupt; no backups on disk at all → callers learn there is no
			// recovery point available for this slot.
			UE_LOG(LogFlecsSave, Error,
				TEXT("ReadSaveFileWithBackups: '%s' main file failed (%d) and no backup files exist"),
				*SlotName, (int32)R);
			return ELoadResult::AllBackupsFailed;
		}

		// Backups existed but every one failed verification — preserve the main failure
		// code so the UI can show a meaningful reason (CRC vs version vs decompress).
		return R;
	}
}
