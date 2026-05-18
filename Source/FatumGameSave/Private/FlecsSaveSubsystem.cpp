// UFlecsSaveSubsystem — central save/load orchestration. Phase 1: pipeline plumbing
// + smoke-test path. Real entity walking lands in Phase 2.

#include "FlecsSaveSubsystem.h"
#include "FlecsSaveFileFormat.h"
#include "FlecsSaveFileIO.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveSnapshotWriter.h"
#include "FlecsSaveSnapshotReader.h"

#include "FSimulationWorker.h"
#include "FlecsArtillerySubsystem.h"

#include "Async/Async.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/WeakObjectPtr.h"

// ═══════════════════════════════════════════════════════════════
// SLOT NAMING (v2 §m3 — Slot_NN, zero-padded, capital S)
// ═══════════════════════════════════════════════════════════════

FString UFlecsSaveSubsystem::GetSlotNameForIndex(int32 SlotIndex)
{
	checkf(SlotIndex >= 0 && SlotIndex < kTotalSlots,
		TEXT("UFlecsSaveSubsystem: SlotIndex %d out of range [0, %d)"),
		SlotIndex, kTotalSlots);
	return FString::Printf(TEXT("Slot_%02d"), SlotIndex);
}

// ═══════════════════════════════════════════════════════════════
// SUBSYSTEM LIFECYCLE
// ═══════════════════════════════════════════════════════════════

void UFlecsSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogFlecsSave, Log, TEXT("UFlecsSaveSubsystem: Initialize"));
}

void UFlecsSaveSubsystem::Deinitialize()
{
	// Cancel any pending deferred-load tick — the lambda holds a TWeakObjectPtr so it
	// is self-guarding, but removing the ticker eliminates the wasted invocation.
	if (DeferredLoadHandle.IsValid())
	{
		FTSTicker::RemoveTicker(DeferredLoadHandle);
		DeferredLoadHandle.Reset();
	}

	// Drop any in-flight reader so the underlying buffer is released eagerly.
	PendingReader.Reset();

	UE_LOG(LogFlecsSave, Log, TEXT("UFlecsSaveSubsystem: Deinitialize"));
	Super::Deinitialize();
}

// ═══════════════════════════════════════════════════════════════
// HELPERS (v2 §5.9 — triple-null-guarded artillery accessor)
// ═══════════════════════════════════════════════════════════════

UFlecsArtillerySubsystem* UFlecsSaveSubsystem::GetArtillery() const
{
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		UE_LOG(LogFlecsSave, Warning, TEXT("GetArtillery: no GameInstance"));
		return nullptr;
	}
	UWorld* W = GI->GetWorld();
	if (!W)
	{
		UE_LOG(LogFlecsSave, Warning, TEXT("GetArtillery: no World on GameInstance"));
		return nullptr;
	}
	UFlecsArtillerySubsystem* Sub = W->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!Sub)
	{
		UE_LOG(LogFlecsSave, Warning, TEXT("GetArtillery: artillery subsystem not yet initialized"));
		return nullptr;
	}
	return Sub;
}

FSimulationWorker* UFlecsSaveSubsystem::GetWorker() const
{
	UFlecsArtillerySubsystem* Artillery = GetArtillery();
	return Artillery ? &Artillery->GetSimWorker() : nullptr;
}

// ═══════════════════════════════════════════════════════════════
// SAVE PATH
// ═══════════════════════════════════════════════════════════════

ESaveResult UFlecsSaveSubsystem::RequestSave(int32 SlotIndex, const FString& DisplayName)
{
	check(IsInGameThread());

	if (SlotIndex < 0 || SlotIndex >= kTotalSlots)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("RequestSave: slot %d out of range [0, %d)"),
			SlotIndex, kTotalSlots);
		return ESaveResult::UnknownError;
	}

	// Single-flight guard — caller polls IsSaveInProgress to back off.
	if (bSaveBusy.exchange(true))
	{
		UE_LOG(LogFlecsSave, Warning, TEXT("RequestSave: a save is already in progress"));
		return ESaveResult::AlreadyInProgress;
	}

	FSimulationWorker* Worker = GetWorker();
	if (!Worker)
	{
		bSaveBusy.store(false);
		return ESaveResult::NoWorld;
	}

	// Heap-shared writer (v2 §5.11) — captured by value into the sim-thread lambda;
	// released by whichever thread drops the last ref.
	TSharedPtr<FFlecsSaveSnapshotWriter, ESPMode::ThreadSafe> Writer =
		MakeShared<FFlecsSaveSnapshotWriter, ESPMode::ThreadSafe>();

	UE_LOG(LogFlecsSave, Log,
		TEXT("RequestSave: scheduling snapshot for %s ('%s')"),
		*GetSlotNameForIndex(SlotIndex), *DisplayName);

	// Step 1 — sim-thread snapshot walk under the sequence fence.
	// Lambda captures Worker* by value so the per-v3 D rule is satisfied (never capture
	// flecs::world& by reference — outer stack frame is gone by lambda execution).
	const uint64 SnapSeq = Worker->EnqueueSeqCommand(
		[Writer, Worker]() mutable
		{
			// Phase 1: World pointer is accepted but not read by the stub.
			flecs::world* WorldPtr = nullptr;
			if (Worker && Worker->FlecsSubsystem)
			{
				WorldPtr = Worker->FlecsSubsystem->GetFlecsWorld();
			}
			Writer->WalkAndSerialize(WorldPtr);
		});

	if (!Worker->WaitForSequence(SnapSeq, /*TimeoutSeconds=*/ 2.0))
	{
		UE_LOG(LogFlecsSave, Error, TEXT("RequestSave: snapshot fence timeout"));
		bSaveBusy.store(false);
		return ESaveResult::Timeout;
	}

	UE_LOG(LogFlecsSave, Verbose,
		TEXT("RequestSave: snapshot ready (%d uncompressed bytes); compressing on pool"),
		Writer->GetSerializedBytes().Num());

	// Step 2 — background-pool compression (v2 §m4 + §M12).
	// Both this lambda and the game-thread continuation capture WeakThis to survive
	// PIE-stop / GameInstance teardown without UAF.
	TWeakObjectPtr<UFlecsSaveSubsystem> WeakThis(this);
	const FString DisplayCopy = DisplayName;
	const int32 SlotIndexCopy = SlotIndex;
	CompressOnPool(WeakThis, SlotIndexCopy, DisplayCopy, Writer);

	return ESaveResult::Pending;
}

ESaveResult UFlecsSaveSubsystem::RequestQuicksave()
{
	return RequestSave(kQuicksaveSlot, TEXT("Quicksave"));
}

void UFlecsSaveSubsystem::CompressOnPool(
	TWeakObjectPtr<UFlecsSaveSubsystem> WeakThis,
	int32 SlotIndex,
	FString DisplayName,
	TSharedPtr<FFlecsSaveSnapshotWriter, ESPMode::ThreadSafe> Writer)
{
	// Cache the uncompressed size on the game thread BEFORE the async dispatch — the
	// writer's buffer is moved into the lambda but the size is needed in the header.
	const uint32 UncompressedSize = static_cast<uint32>(Writer->GetSerializedBytes().Num());

	Async(EAsyncExecution::ThreadPool,
		[WeakThis, SlotIndex, DisplayName, Writer, UncompressedSize]()
		{
			// First WeakThis check — quick bail on subsystem GC.
			if (!WeakThis.IsValid())
			{
				UE_LOG(LogFlecsSave, Warning, TEXT("Save abandoned during compress — subsystem GC'd"));
				return;
			}

			TArray<uint8> Compressed;
			const bool bOk = FlecsSaveIO::CompressPayload(Writer->GetSerializedBytes(), Compressed);

			// Game-thread continuation. Capture WeakThis again — the compress lambda may have
			// run on the background pool for an arbitrary duration, and the subsystem could
			// have been destroyed between the two awaits.
			AsyncTask(ENamedThreads::GameThread,
				[WeakThis, SlotIndex, DisplayName,
				 Compressed = MoveTemp(Compressed), UncompressedSize, bOk]() mutable
				{
					UFlecsSaveSubsystem* Self = WeakThis.Get();
					if (!Self)
					{
						UE_LOG(LogFlecsSave, Warning, TEXT("Save abandoned during disk-write — subsystem GC'd"));
						return;
					}
					Self->OnAsyncSaveComplete(SlotIndex, DisplayName, MoveTemp(Compressed), UncompressedSize, bOk);
				});
		});
}

void UFlecsSaveSubsystem::OnAsyncSaveComplete(int32 SlotIndex, FString DisplayName, TArray<uint8> Compressed, uint32 UncompressedSize, bool bCompressOk)
{
	check(IsInGameThread());

	// We are the sole writer of bSaveBusy from now until broadcast — clear it before
	// the broadcast so listeners can immediately re-trigger if they want.
	const FString SlotName = GetSlotNameForIndex(SlotIndex);

	ESaveResult Result;
	if (!bCompressOk)
	{
		Result = ESaveResult::CompressFailed;
	}
	else
	{
		Result = FlecsSaveIO::WriteSaveFile(SlotName, Compressed, UncompressedSize);
	}

	bSaveBusy.store(false);

	UE_LOG(LogFlecsSave, Log,
		TEXT("OnAsyncSaveComplete: slot %d ('%s') name='%s' result=%d (%d compressed bytes)"),
		SlotIndex, *SlotName, *DisplayName, (int32)Result, Compressed.Num());

	OnSaveComplete.Broadcast(SlotIndex, SlotName, Result);
}

// ═══════════════════════════════════════════════════════════════
// LOAD PATH (v2 §5.4 — one-tick deferred via FTSTicker, C4 fix)
// ═══════════════════════════════════════════════════════════════

ELoadResult UFlecsSaveSubsystem::RequestLoad(int32 SlotIndex)
{
	check(IsInGameThread());

	if (SlotIndex < 0 || SlotIndex >= kTotalSlots)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("RequestLoad: slot %d out of range [0, %d)"),
			SlotIndex, kTotalSlots);
		return ELoadResult::UnknownError;
	}

	if (bLoadBusy.exchange(true))
	{
		UE_LOG(LogFlecsSave, Warning, TEXT("RequestLoad: a load is already in progress"));
		return ELoadResult::AbortedAsync;
	}

	if (DeferredLoadHandle.IsValid())
	{
		// Defensive — should not happen because bLoadBusy gates entry, but if it does
		// the previous ticker is stale and must be cancelled.
		FTSTicker::RemoveTicker(DeferredLoadHandle);
		DeferredLoadHandle.Reset();
	}

	const FString SlotName = GetSlotNameForIndex(SlotIndex);

	// ── Phase A: synchronous read + verify + decompress on game thread ────
	// These are fast (milliseconds for the Phase 1 stub) and let us fail-fast BEFORE
	// scheduling the wipe + decode work that tears down the world.
	TArray<uint8> Uncompressed;
	const ELoadResult ReadResult = FlecsSaveIO::ReadSaveFileWithBackups(SlotName, Uncompressed);
	if (ReadResult != ELoadResult::Success)
	{
		bLoadBusy.store(false);
		UE_LOG(LogFlecsSave, Warning,
			TEXT("RequestLoad: slot %d ('%s') read failed (%d)"),
			SlotIndex, *SlotName, (int32)ReadResult);
		OnLoadComplete.Broadcast(SlotIndex, SlotName, ReadResult);
		return ReadResult;
	}

	// Heap-shared reader (v2 §5.11 + C2 fix). Owns its payload buffer; safe to capture
	// by value into a lambda that may outlive this function's stack frame.
	PendingReader = MakeShared<FFlecsSaveSnapshotReader, ESPMode::ThreadSafe>(MoveTemp(Uncompressed));

	UE_LOG(LogFlecsSave, Log,
		TEXT("RequestLoad: slot %d ('%s') decoded; deferring apply to next tick"),
		SlotIndex, *SlotName);

	// ── Phase B: defer the apply step ONE tick via FTSTicker ──────────────
	// The call stack of the input handler / overlap that invoked RequestLoad has fully
	// unwound by the time FTSTicker fires (C4 fix). Without this, calling Destroy() on
	// the player actor from inside its own input dispatch would destroy the actor that
	// owns the current call stack → UB.
	TWeakObjectPtr<UFlecsSaveSubsystem> WeakThis(this);
	DeferredLoadHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda(
			[WeakThis, SlotIndex](float /*DeltaT*/) -> bool
			{
				if (UFlecsSaveSubsystem* Self = WeakThis.Get())
				{
					Self->DeferredLoadTick(SlotIndex);
				}
				return false;   // single-shot
			}),
		/*Delay=*/ 0.0f);

	return ELoadResult::Pending;
}

ELoadResult UFlecsSaveSubsystem::RequestQuickload()
{
	return RequestLoad(kQuicksaveSlot);
}

void UFlecsSaveSubsystem::DeferredLoadTick(int32 SlotIndex)
{
	check(IsInGameThread());

	// Reset our handle — the ticker has fired and will not fire again (returned false).
	DeferredLoadHandle.Reset();

	const FString SlotName = GetSlotNameForIndex(SlotIndex);

	// Move the reader off the subsystem so any re-entrant load can scribble a new one.
	TSharedPtr<FFlecsSaveSnapshotReader, ESPMode::ThreadSafe> Reader = MoveTemp(PendingReader);
	if (!Reader.IsValid())
	{
		UE_LOG(LogFlecsSave, Error, TEXT("DeferredLoadTick: no pending reader"));
		bLoadBusy.store(false);
		OnLoadComplete.Broadcast(SlotIndex, SlotName, ELoadResult::UnknownError);
		return;
	}

	UFlecsArtillerySubsystem* Artillery = GetArtillery();
	if (!Artillery)
	{
		bLoadBusy.store(false);
		OnLoadComplete.Broadcast(SlotIndex, SlotName, ELoadResult::NoWorld);
		return;
	}
	FSimulationWorker* Worker = GetWorker();
	if (!Worker)
	{
		bLoadBusy.store(false);
		OnLoadComplete.Broadcast(SlotIndex, SlotName, ELoadResult::NoWorld);
		return;
	}

	// ── PHASE 1 STUB WIPE ──────────────────────────────────────────────────
	// Real wipe lives in Phase 5 (per v3 §A — WipeEntitiesByTag with prefab protection).
	// Phase 1 doesn't actually wipe anything — we just log that we would.
	UE_LOG(LogFlecsSave, Log, TEXT("DeferredLoadTick (Phase 1 stub): would wipe live entities here"));

	// ── PHASE 1 STUB DECODE ────────────────────────────────────────────────
	// Reader is captured by value (TSharedPtr) — kept alive across the sim-thread sync.
	// Per v3 D.1: capture Worker by value, never capture flecs::world& by reference.
	//
	// Result flag is heap-shared (TSharedPtr<std::atomic<bool>>) so the lambda can
	// publish the decode outcome without depending on this stack frame's lifetime.
	// The acquire/release ordering on CompletedCommandSeq ensures the atomic write is
	// visible to the game thread after WaitForSequence returns true.
	TSharedPtr<std::atomic<bool>, ESPMode::ThreadSafe> DecodeAccepted =
		MakeShared<std::atomic<bool>, ESPMode::ThreadSafe>(false);

	const uint64 DecodeSeq = Worker->EnqueueSeqCommand(
		[Reader, Worker, DecodeAccepted]() mutable
		{
			flecs::world* WorldPtr = nullptr;
			if (Worker && Worker->FlecsSubsystem)
			{
				WorldPtr = Worker->FlecsSubsystem->GetFlecsWorld();
			}
			const bool bOk = Reader->ApplyToFlecsWorld(WorldPtr);
			DecodeAccepted->store(bOk, std::memory_order_release);
		});

	// Wait under fence — propagate timeout to the broadcast.
	if (!Worker->WaitForSequence(DecodeSeq, /*TimeoutSeconds=*/ 5.0))
	{
		UE_LOG(LogFlecsSave, Error, TEXT("DeferredLoadTick: decode fence timeout"));
		bLoadBusy.store(false);
		OnLoadComplete.Broadcast(SlotIndex, SlotName, ELoadResult::Timeout);
		return;
	}

	const ELoadResult Result = DecodeAccepted->load(std::memory_order_acquire)
		? ELoadResult::Success
		: ELoadResult::UnknownError;

	bLoadBusy.store(false);
	UE_LOG(LogFlecsSave, Log,
		TEXT("DeferredLoadTick: slot %d ('%s') result=%d"),
		SlotIndex, *SlotName, (int32)Result);
	OnLoadComplete.Broadcast(SlotIndex, SlotName, Result);
}
