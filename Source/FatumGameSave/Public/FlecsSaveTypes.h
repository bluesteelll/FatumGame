// FlecsSaveTypes — shared enums/structs for the save subsystem public API.
//
// Per v2 §m1: every ESaveResult / ELoadResult value MUST be returned from at least
// one codepath. Values that are not yet wired are commented "// reserved for Phase N".

#pragma once

#include "CoreMinimal.h"
#include "FlecsSaveTypes.generated.h"

// ═══════════════════════════════════════════════════════════════
// SAVE RESULT ENUM
// ═══════════════════════════════════════════════════════════════

/** Final result of an asynchronous save request. Broadcast via OnSaveComplete. */
UENUM(BlueprintType)
enum class ESaveResult : uint8
{
	/** Save was successfully written, fsync'd, and atomically renamed. */
	Success,

	/** Save was scheduled and is now in-flight. Used as the synchronous return from RequestSave. */
	Pending,

	/** No active world or GameInstance. Save subsystem refused; nothing was attempted. */
	NoWorld,

	/** A save was already in progress when the request arrived. Caller must wait. */
	AlreadyInProgress,

	/** Sequence-fenced snapshot walk did not complete before the timeout deadline. */
	Timeout,

	/** Compression of the snapshot blob failed. */
	CompressFailed,

	/** Disk I/O failed (open/write/flush/rename returned an error). */
	IOFailed,

	/** Subsystem was garbage-collected before async completion. */
	AbortedAsync,

	/** Catch-all for unanticipated failures. Should never be returned in a green build. */
	UnknownError,
};

// ═══════════════════════════════════════════════════════════════
// LOAD RESULT ENUM
// ═══════════════════════════════════════════════════════════════

/** Final result of an asynchronous load request. Broadcast via OnLoadComplete. */
UENUM(BlueprintType)
enum class ELoadResult : uint8
{
	/** Load was successfully applied to the world. */
	Success,

	/** Load was scheduled and the deferred-load tick has yet to run. */
	Pending,

	/** Slot file does not exist on disk. */
	FileMissing,

	/** Header magic bytes do not match FatumSave::kMagic ('FTSV'). */
	MagicMismatch,

	/** Header version is outside [kMinVersion, kVersion]. */
	VersionMismatch,

	/** CRC32 of compressed payload does not match header. Backups also tried and failed. */
	CrcMismatch,

	/** Decompression of the validated blob failed. */
	DecompressFail,

	/** Main file was corrupt AND no backup files existed to recover from (Phase 7). */
	AllBackupsFailed,

	/** Save was for a different level than the currently loaded level (cross-level out of scope per Q10). */
	WorldMismatch,

	/** No active world or GameInstance. */
	NoWorld,

	/** Sequence-fenced wipe / decode / rebind did not complete before the timeout deadline. */
	Timeout,

	/** Subsystem was garbage-collected before async completion. */
	AbortedAsync,

	/** Catch-all for unanticipated failures. Should never be returned in a green build. */
	UnknownError,
};

// ═══════════════════════════════════════════════════════════════
// SAVE SLOT INFO (BP-facing)
// ═══════════════════════════════════════════════════════════════

/** Summary of a slot's on-disk state — used by save/load UI. */
USTRUCT(BlueprintType)
struct FATUMGAMESAVE_API FSaveSlotInfo
{
	GENERATED_BODY()

	/** "Slot_00" through "Slot_11" (zero-padded). */
	UPROPERTY(BlueprintReadOnly, Category = "Save")
	FString SlotName;

	/** User-supplied display name (set via RequestSave). Empty for unwritten slots. */
	UPROPERTY(BlueprintReadOnly, Category = "Save")
	FString DisplayName;

	/** File modification timestamp. */
	UPROPERTY(BlueprintReadOnly, Category = "Save")
	FDateTime Timestamp;

	/** Level name that was active when the save was written. Empty when unknown. */
	UPROPERTY(BlueprintReadOnly, Category = "Save")
	FString LevelName;

	/** GameBuildHash recorded in the save header. 0 when the file does not exist. */
	UPROPERTY(BlueprintReadOnly, Category = "Save")
	int64 GameBuildHash = 0;

	/** Size of the slot file in bytes (compressed). 0 when missing. */
	UPROPERTY(BlueprintReadOnly, Category = "Save")
	int64 FileSizeBytes = 0;

	/** True if the slot file exists on disk. */
	UPROPERTY(BlueprintReadOnly, Category = "Save")
	bool bExists = false;

	/** True if the file exists but failed magic / version / CRC checks. */
	UPROPERTY(BlueprintReadOnly, Category = "Save")
	bool bCorrupt = false;
};
