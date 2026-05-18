// UFlecsSaveSubsystem — central save/load orchestration. Phase 5: real wipe +
// Barrage body restore + spawner dedup + BladeBuffer realloc.

#include "FlecsSaveSubsystem.h"
#include "FlecsSaveBarrageRestore.h"
#include "FlecsSaveFileFormat.h"
#include "FlecsSaveFileIO.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveSnapshotWriter.h"
#include "FlecsSaveSnapshotReader.h"

#include "FSimulationWorker.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsBarrageComponents.h"        // FBarrageBody for orphan sweep
#include "FlecsGameTags.h"                  // FTagProjectile / FTagItem / FTagContainer / etc.
#include "FlecsMeleeComponents.h"           // FTagMeleeAttacking / FTagMeleeCharging / FMeleeWeaponInstance
#include "FlecsWeaponComponents.h"          // FTagChargingWeapon
#include "FlecsCraftingComponents.h"        // FTagCraftingStation
#include "FlecsDoorComponents.h"            // FTagDoor (declared but actually in FlecsGameTags.h — both kept for safety)
#include "FlecsSpawnerComponents.h"         // FSpawnerProvenance
#include "FlecsSaveTags.h"                  // FTagPlayerCharacter

#include "FlecsEntitySpawnerActor.h"        // AFlecsEntitySpawner + bSavedEntityOverridesMe

#include "Library/FlecsMeleeEquipHelpers.h" // AllocateBladeBufferForMeleeEntity (post-decode pass)

#include "Async/Async.h"
#include "EngineUtils.h"                   // TActorIterator
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"               // IFileManager (Phase 6 — DeleteSlot)
#include "UObject/WeakObjectPtr.h"

#include "flecs.h"

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
// PHASE 5 — PREFAB-PROTECTING WIPE (v3 §A)
// ═══════════════════════════════════════════════════════════════
//
// WipeEntitiesByTag — collects non-prefab entities matching a tag id (uses
// .with(flecs::Prefab).oper(flecs::Not) per v3 §A so prefabs survive), unbinds
// FBarrageBody primitives via DEBRIS layer + tombstone, then destructs each entity.
//
// Order of invocations matters: combat state -> pair entities -> gameplay. The
// 13-tag invocation list (per v3 §A.3) sits inside DeferredLoadTick.
//
// Sim-thread only.

namespace
{
	void WipeEntitiesByTag(
		UFlecsArtillerySubsystem* Artillery,
		flecs::world& World,
		flecs::entity_t TagId,
		const TCHAR* TagDebugName)
	{
		check(Artillery);

		TArray<flecs::entity_t> ToDelete;
		ToDelete.Reserve(256);

		World.query_builder<>()
			.with(TagId)
			.with(flecs::Prefab).oper(flecs::Not)
			.build()
			.each([&ToDelete](flecs::entity E)
			{
				ToDelete.Add(E.id());
			});

		int32 DestructedCount = 0;
		for (flecs::entity_t Id : ToDelete)
		{
			flecs::entity E(World, Id);
			if (!E.is_alive()) continue;
			if (E.has(flecs::Prefab))
			{
				ensureMsgf(false,
					TEXT("WipeEntitiesByTag(%s): prefab id %llu slipped through filter"),
					TagDebugName, static_cast<uint64>(Id));
				continue;
			}
			if (const FBarrageBody* Body = E.try_get<FBarrageBody>())
			{
				FlecsSaveBarrage::UnbindAndTombstoneBarrageBody(Artillery, *Body);
			}
			E.destruct();
			++DestructedCount;
		}

		UE_LOG(LogFlecsSave, Verbose,
			TEXT("WipeEntitiesByTag(%s): destructed %d entities"),
			TagDebugName, DestructedCount);
	}

	void WipeOrphanBarrageBodies(
		UFlecsArtillerySubsystem* Artillery,
		flecs::world& World)
	{
		check(Artillery);

		TArray<flecs::entity_t> Orphans;
		World.query_builder<>()
			.with<FBarrageBody>()
			.with(flecs::Prefab).oper(flecs::Not)
			.build()
			.each([&Orphans](flecs::entity E)
			{
				Orphans.Add(E.id());
			});

		int32 DestructedCount = 0;
		for (flecs::entity_t Id : Orphans)
		{
			flecs::entity E(World, Id);
			if (!E.is_alive() || E.has(flecs::Prefab)) continue;
			if (const FBarrageBody* Body = E.try_get<FBarrageBody>())
			{
				FlecsSaveBarrage::UnbindAndTombstoneBarrageBody(Artillery, *Body);
			}
			E.destruct();
			++DestructedCount;
		}

		if (DestructedCount > 0)
		{
			UE_LOG(LogFlecsSave, Warning,
				TEXT("WipeOrphanBarrageBodies: %d entities with FBarrageBody escaped tag-based wipe"),
				DestructedCount);
		}
	}
}

// Number of tag-driven wipe invocations. If you add a new tag, bump this count AND
// add the call in DoFullWipe. Mirrors v3 §A.4 defense.
constexpr int32 kSaveWipeTagDomainCount = 13;

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
			// Phase 2: writer requires a valid world. If we somehow reached the fence
			// without the FlecsSubsystem being set, fail loudly — silent zero-entity
			// snapshots would erase the player's progress.
			flecs::world* WorldPtr = nullptr;
			if (Worker && Worker->FlecsSubsystem)
			{
				WorldPtr = Worker->FlecsSubsystem->GetFlecsWorld();
			}
			checkf(WorldPtr,
				TEXT("Save: sim-thread lambda has no Flecs world — FlecsSubsystem missing or torn down"));
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

// ═══════════════════════════════════════════════════════════════
// SLOT MAINTENANCE — DeleteSlot (Phase 6)
// ═══════════════════════════════════════════════════════════════
//
// Removes the main .sav and every backup generation for a slot. Refuses while a
// save or load is in flight: deleting the file the I/O code path is reading or
// the rotation chain it is writing into would be unrecoverable. Game-thread only;
// IFileManager calls are synchronous and small (~4 file ops).

bool UFlecsSaveSubsystem::DeleteSlot(int32 SlotIndex)
{
	check(IsInGameThread());

	if (SlotIndex < 0 || SlotIndex >= kTotalSlots)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("DeleteSlot: slot %d out of range [0, %d)"),
			SlotIndex, kTotalSlots);
		return false;
	}

	if (bSaveBusy.load() || bLoadBusy.load())
	{
		UE_LOG(LogFlecsSave, Warning,
			TEXT("DeleteSlot: refused — save=%d load=%d in progress"),
			(int32)bSaveBusy.load(), (int32)bLoadBusy.load());
		return false;
	}

	const FString SlotName = GetSlotNameForIndex(SlotIndex);
	IFileManager& FM = IFileManager::Get();

	int32 DeletedCount = 0;
	// Gen 0 = main .sav, Gen 1..kBackupChainDepth = .bak1..bak3 (FlecsSaveIO::kBackupChainDepth).
	for (int32 Gen = 0; Gen <= FlecsSaveIO::kBackupChainDepth; ++Gen)
	{
		const FString Path = FlecsSaveIO::GetSlotFilePath(SlotName, Gen);
		if (!FM.FileExists(*Path))
		{
			continue;
		}
		if (FM.Delete(*Path, /*RequireExists=*/ false, /*EvenIfReadOnly=*/ false, /*Quiet=*/ true))
		{
			++DeletedCount;
		}
		else
		{
			UE_LOG(LogFlecsSave, Warning,
				TEXT("DeleteSlot: failed to delete '%s'"), *Path);
		}
	}

	UE_LOG(LogFlecsSave, Log,
		TEXT("DeleteSlot: slot %d ('%s') deleted %d file(s)"),
		SlotIndex, *SlotName, DeletedCount);
	return DeletedCount > 0;
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

	// ─── STEP 1 — Pre-wipe spawner mark pass (game thread) ─────────────────────
	// Walk the loaded snapshot for FSpawnerProvenance tuples and mark matching
	// AFlecsEntitySpawner actors with bSavedEntityOverridesMe. This MUST happen BEFORE
	// the wipe (in case the actors are about to fire their BeginPlay) AND before the
	// decode (so freshly-loaded entities are the sole authority). Per v3 §A.3 second half.
	MarkOverriddenSpawners(*Reader);

	// ─── STEP 2 — Sim-thread wipe + decode + post-decode rebind ─────────────────
	// Three logical phases inside a single sim-thread lambda. Per v3 §D the lambda
	// captures `Worker` by value and resolves the Flecs world via Worker->GetFlecsWorld()
	// at execution time — never captures the world reference directly (outer stack frame
	// is gone by lambda execution).
	//
	// Decode result is published through a heap-shared std::atomic so the game thread
	// can read it after WaitForSequence returns. Acquire/release ordering on
	// CompletedCommandSeq ensures visibility.
	TSharedPtr<std::atomic<bool>, ESPMode::ThreadSafe> DecodeAccepted =
		MakeShared<std::atomic<bool>, ESPMode::ThreadSafe>(false);

	const uint64 DecodeSeq = Worker->EnqueueSeqCommand(
		[Reader, Worker, DecodeAccepted, Artillery]() mutable
		{
			flecs::world* WorldPtr = nullptr;
			if (Worker && Worker->FlecsSubsystem)
			{
				WorldPtr = Worker->FlecsSubsystem->GetFlecsWorld();
			}
			if (!WorldPtr)
			{
				UE_LOG(LogFlecsSave, Error,
					TEXT("Load: sim-thread lambda has no Flecs world — FlecsSubsystem missing or torn down"));
				DecodeAccepted->store(false, std::memory_order_release);
				return;
			}

			// ── Phase A: WIPE (v3 §A.3 13 invocations + orphan sweep) ───────
			// Order matters: kill aggressors before victims to avoid spurious contact
			// events during destruction. Prefabs are protected by the helper's
			// .with(flecs::Prefab).oper(flecs::Not) filter.
			flecs::world& W = *WorldPtr;
			WipeEntitiesByTag(Artillery, W, W.component<FTagProjectile>(),         TEXT("FTagProjectile"));
			WipeEntitiesByTag(Artillery, W, W.component<FTagDebrisFragment>(),     TEXT("FTagDebrisFragment"));
			WipeEntitiesByTag(Artillery, W, W.component<FTagMeleeAttacking>(),     TEXT("FTagMeleeAttacking"));
			WipeEntitiesByTag(Artillery, W, W.component<FTagMeleeCharging>(),      TEXT("FTagMeleeCharging"));
			WipeEntitiesByTag(Artillery, W, W.component<FTagChargingWeapon>(),     TEXT("FTagChargingWeapon"));
			WipeEntitiesByTag(Artillery, W, W.component<FCollisionPair>(),         TEXT("FCollisionPair"));
			WipeEntitiesByTag(Artillery, W, W.component<FTagItem>(),               TEXT("FTagItem"));
			WipeEntitiesByTag(Artillery, W, W.component<FTagContainer>(),          TEXT("FTagContainer"));
			WipeEntitiesByTag(Artillery, W, W.component<FTagDoor>(),               TEXT("FTagDoor"));
			WipeEntitiesByTag(Artillery, W, W.component<FTagInteractable>(),       TEXT("FTagInteractable"));
			WipeEntitiesByTag(Artillery, W, W.component<FTagCraftingStation>(),    TEXT("FTagCraftingStation"));
			WipeEntitiesByTag(Artillery, W, W.component<FTagDestructible>(),       TEXT("FTagDestructible"));
			WipeEntitiesByTag(Artillery, W, W.component<FTagCharacter>(),          TEXT("FTagCharacter"));

			// Defensive orphan sweep — any non-prefab entity that still carries
			// FBarrageBody but escaped the tag-based wipe (logged as Warning).
			WipeOrphanBarrageBodies(Artillery, W);

			// ── Phase B: DECODE (creates entities, populates components, restores bodies) ──
			const bool bOk = Reader->ApplyToFlecsWorld(WorldPtr);
			if (!bOk)
			{
				DecodeAccepted->store(false, std::memory_order_release);
				return;
			}

			// ── Phase C: Post-decode rebind passes ──────────────────────────
			// BladeBuffer realloc for every loaded FMeleeWeaponInstance entity.
			// The encoder skipped the pointer; the decoder zeroed it; the buffer
			// itself must be re-allocated on the sim thread (raw new/delete).
			{
				int32 RebuildCount = 0;
				W.query_builder<FMeleeWeaponInstance>()
					.with(flecs::Prefab).oper(flecs::Not)
					.build()
					.each([&RebuildCount](flecs::entity E, FMeleeWeaponInstance&)
					{
						FlecsMeleeEquip::AllocateBladeBufferForMeleeEntity(E);
						++RebuildCount;
					});
				UE_LOG(LogFlecsSave, Log,
					TEXT("DeferredLoadTick: re-allocated %d melee blade buffers post-decode"),
					RebuildCount);
			}

			// TODO(Phase 6/7): post-load DoorSystem will regenerate ConstraintKey for
			// every FDoorInstance whose ConstraintKey == 0 on first tick. Reuses the
			// existing constraint-creation code path in FlecsArtillerySubsystem_DoorSystems.

			DecodeAccepted->store(true, std::memory_order_release);
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

	// TODO(Phase 6/7): Step 3 — RebindPlayerActorToLoadedEntity per v3 §C.2 (find unique
	// FTagPlayerCharacter holder + bridge re-registration via EnqueueSeqCommand). Phase 5
	// ships the registration hook (AFlecsCharacter::BindToRestoredEntity) as a stub.

	bLoadBusy.store(false);
	UE_LOG(LogFlecsSave, Log,
		TEXT("DeferredLoadTick: slot %d ('%s') result=%d"),
		SlotIndex, *SlotName, (int32)Result);
	OnLoadComplete.Broadcast(SlotIndex, SlotName, Result);
}

// ═══════════════════════════════════════════════════════════════
// PHASE 5 — PRE-LOAD SPAWNER MARK PASS (v2 §5.3 + v3 §A.3)
// ═══════════════════════════════════════════════════════════════

void UFlecsSaveSubsystem::MarkOverriddenSpawners(FFlecsSaveSnapshotReader& Reader)
{
	check(IsInGameThread());

	// Collect the (LevelPath, ActorName) tuples for every FSpawnerProvenance saved.
	TSet<TPair<FName, FName>> Overridden;
	if (!Reader.CollectSavedSpawnerProvenance(Overridden))
	{
		UE_LOG(LogFlecsSave, Warning,
			TEXT("MarkOverriddenSpawners: failed to collect provenance from snapshot — skipping mark pass"));
		return;
	}

	if (Overridden.Num() == 0)
	{
		UE_LOG(LogFlecsSave, Verbose,
			TEXT("MarkOverriddenSpawners: snapshot has no spawner-derived entities — nothing to mark"));
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World)
	{
		UE_LOG(LogFlecsSave, Warning,
			TEXT("MarkOverriddenSpawners: no World — skipping mark pass (spawner dedup will misfire)"));
		return;
	}

	int32 MarkedCount = 0;
	for (TActorIterator<AFlecsEntitySpawner> It(World); It; ++It)
	{
		AFlecsEntitySpawner* Spawner = *It;
		if (!Spawner) continue;

		// Strip PIE prefix from the live spawner's level path so saves taken in PIE
		// match loads in PIE regardless of the UEDPIE_N_ instance id.
		const FString RawLevelPath = Spawner->GetLevel() ? Spawner->GetLevel()->GetPathName() : FString();
		const FString LevelPath = UWorld::RemovePIEPrefix(RawLevelPath);
		const FName LevelPathName(*LevelPath);
		const FName ActorName = Spawner->GetFName();

		if (Overridden.Contains({LevelPathName, ActorName}))
		{
			Spawner->bSavedEntityOverridesMe = true;
			++MarkedCount;
		}
	}

	UE_LOG(LogFlecsSave, Log,
		TEXT("MarkOverriddenSpawners: marked %d spawners from %d saved provenance tuples"),
		MarkedCount, Overridden.Num());
}
