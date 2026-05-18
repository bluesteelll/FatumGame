// UFlecsSaveSubsystem — central save/load orchestration.
//
// UGameInstanceSubsystem (NOT WorldSubsystem) — persists across travel/load so the
// pending-load reader/state survives a level transition.
//
// Pipeline (per v2 §5.4 + §5.10):
//
//   GAME THREAD                          SIM THREAD                       BACKGROUND POOL
//   ───────────                          ──────────                       ────────────────
//   RequestSave(slot, name)
//     bSaveBusy = true
//     Create TSharedPtr<FFlecsSaveSnapshotWriter> Writer
//     Seq = Worker->EnqueueSeqCommand([Writer]{ Writer->WalkAndSerialize(World); })
//     Worker->WaitForSequence(Seq, 2.0)
//                                                                          Async(ThreadPool, [Writer]{
//                                                                            Compress (Oodle BiasSpeed)
//                                                                            AsyncTask(GameThread, [bytes]{
//                                                                              OnAsyncSaveComplete:
//                                                                                Compose header, CRC
//                                                                                Write tmp + fsync + rename
//                                                                                Rotate backups
//                                                                                Broadcast OnSaveComplete
//                                                                            })
//                                                                          })
//
//   RequestLoad(slot)                    [Phase 2+ load path lives here]
//     Phase 1: read+verify+decompress, then reader stub validates 0-entity payload.
//
// PHASE 1: snapshot stub writer emits 24 bytes (header+footer, 0 entities). Reader
// stub validates the payload. The pipeline plumbing — sequence fence, async pool,
// compression, CRC, atomic write, backup rotation, fsync — is all real and exercised.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "FlecsSaveTypes.h"
#include <atomic>
#include "FlecsSaveSubsystem.generated.h"

class UFlecsArtillerySubsystem;
class FSimulationWorker;
class FFlecsSaveSnapshotWriter;
class FFlecsSaveSnapshotReader;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnSaveComplete, int32, SlotIndex, FString, SlotName, ESaveResult, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnLoadComplete, int32, SlotIndex, FString, SlotName, ELoadResult, Result);

UCLASS()
class FATUMGAMESAVE_API UFlecsSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// SLOT MANAGEMENT
	// ═══════════════════════════════════════════════════════════════
	//
	// Slots 0..9 = named saves, slot 10 = Quicksave, slot 11 = LastSession (autosave).
	// Stored as Slot_00.sav through Slot_11.sav (two-digit, zero-padded — per v2 §m3).

	static constexpr int32 kQuicksaveSlot    = 10;
	static constexpr int32 kLastSessionSlot  = 11;
	static constexpr int32 kTotalSlots       = 12;

	UPROPERTY(BlueprintAssignable, Category = "Save")
	FOnSaveComplete OnSaveComplete;

	UPROPERTY(BlueprintAssignable, Category = "Save")
	FOnLoadComplete OnLoadComplete;

	// ═══════════════════════════════════════════════════════════════
	// PUBLIC API (Blueprint-callable)
	// ═══════════════════════════════════════════════════════════════

	/** Begin an asynchronous save to the given slot. Returns Pending on accept;
	 *  final result is broadcast via OnSaveComplete. */
	UFUNCTION(BlueprintCallable, Category = "Save")
	ESaveResult RequestSave(int32 SlotIndex, const FString& DisplayName);

	/** Save to the dedicated quicksave slot. Convenience wrapper. */
	UFUNCTION(BlueprintCallable, Category = "Save")
	ESaveResult RequestQuicksave();

	/** Begin an asynchronous load from the given slot. Returns Pending on accept;
	 *  final result is broadcast via OnLoadComplete. */
	UFUNCTION(BlueprintCallable, Category = "Save")
	ELoadResult RequestLoad(int32 SlotIndex);

	/** Load from the dedicated quicksave slot. Convenience wrapper. */
	UFUNCTION(BlueprintCallable, Category = "Save")
	ELoadResult RequestQuickload();

	/** Delete a slot's files from disk (main .sav + all rolling backups .bak1..bak3).
	 *  Refuses while a save/load is in progress to avoid racing the I/O code. Returns
	 *  true if anything was deleted; false if the slot was already empty / refused. */
	UFUNCTION(BlueprintCallable, Category = "Save")
	bool DeleteSlot(int32 SlotIndex);

	/** True if a save is currently in flight (snapshot walking / compressing / writing). */
	UFUNCTION(BlueprintPure, Category = "Save")
	bool IsSaveInProgress() const { return bSaveBusy.load(); }

	/** True if a load is currently in flight (reading / decoding / wiping / rebinding). */
	UFUNCTION(BlueprintPure, Category = "Save")
	bool IsLoadInProgress() const { return bLoadBusy.load(); }

	/** Compose the standard slot name ("Slot_00" through "Slot_11") for a slot index.
	 *  Static so UI can stringify slot names without holding a subsystem reference. */
	UFUNCTION(BlueprintPure, Category = "Save")
	static FString GetSlotNameForIndex(int32 SlotIndex);

	// ═══════════════════════════════════════════════════════════════
	// UGameInstanceSubsystem
	// ═══════════════════════════════════════════════════════════════

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	// ═══════════════════════════════════════════════════════════════
	// SAVE PATH (orchestration helpers)
	// ═══════════════════════════════════════════════════════════════

	/** Background-pool compression entry point. WeakThis-guarded per v2 §5.10. */
	static void CompressOnPool(
		TWeakObjectPtr<UFlecsSaveSubsystem> WeakThis,
		int32 SlotIndex,
		FString DisplayName,
		TSharedPtr<FFlecsSaveSnapshotWriter, ESPMode::ThreadSafe> Writer);

	/** Game-thread continuation after compress completes. */
	void OnAsyncSaveComplete(int32 SlotIndex, FString DisplayName, TArray<uint8> Compressed, uint32 UncompressedSize, bool bCompressOk);

	// ═══════════════════════════════════════════════════════════════
	// LOAD PATH (orchestration helpers)
	// ═══════════════════════════════════════════════════════════════

	/** Deferred-tick load apply (called one frame after RequestLoad, per v2 §5.4 + C4). */
	void DeferredLoadTick(int32 SlotIndex);

	// ═══════════════════════════════════════════════════════════════
	// CROSS-CUTTING HELPERS
	// ═══════════════════════════════════════════════════════════════

	/** Triple-null-guarded artillery accessor (v2 §5.9). Returns nullptr without
	 *  asserting when the world / GameInstance / artillery subsystem is not present. */
	UFlecsArtillerySubsystem* GetArtillery() const;

	/** Wrapper that follows the same null-guard chain to reach the sim worker. */
	FSimulationWorker* GetWorker() const;

	/** Phase 5 — pre-load spawner mark pass. Scans saved entities for FSpawnerProvenance
	 *  tuples and marks matching AFlecsEntitySpawner actors with bSavedEntityOverridesMe
	 *  so they skip BeginPlay spawn. Game thread, runs BEFORE the wipe + decode passes.
	 *  Sim-thread guarded via try_get accessed on the cached snapshot — no Flecs ops here. */
	void MarkOverriddenSpawners(class FFlecsSaveSnapshotReader& Reader);

	// ═══════════════════════════════════════════════════════════════
	// STATE (game thread)
	// ═══════════════════════════════════════════════════════════════

	std::atomic<bool> bSaveBusy { false };
	std::atomic<bool> bLoadBusy { false };

	/** Pending decoded reader between RequestLoad and DeferredLoadTick. Game thread only. */
	TSharedPtr<FFlecsSaveSnapshotReader, ESPMode::ThreadSafe> PendingReader;

	/** Ticker handle for the deferred-load tick. */
	FTSTicker::FDelegateHandle DeferredLoadHandle;
};
