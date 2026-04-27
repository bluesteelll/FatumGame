// UFlecsCraftingUISubsystem — game module subsystem owning per-station snapshot shared state.
// Mirrors UFlecsUISubsystem::Containers pattern (FContainerSharedState) but keyed by
// FSkeletonKey instead of Flecs entity id, per Blueprint v3 §1.6 / Step 10.
//
// Ownership contract (MJ3):
//   Shared state lives off ECS — TTripleBuffer and std::atomic cannot ride inside a Flecs
//   component because Flecs may memcpy components on archetype migration (UB for atomics).
//   Owner = this subsystem (game-thread TMap). Sim thread publishes via FindSharedState;
//   widgets read via the same FindSharedState on game thread.
//
// Lifecycle:
//   CreateSharedState   — game-thread only, invoked via AsyncTask posted from sim thread
//                         (station spawn — see Spawner Step 13).
//   DestroySharedState  — game-thread only, invoked via AsyncTask posted from sim thread
//                         (station destroy — see Library Step 15).
//   FindSharedState     — callable from either thread; game-thread insertion/removal is
//                         serialised by the TMap's non-rehashing guarantee at Phase-1
//                         scale (≤20 stations). Sim-thread reader null-checks and the
//                         upstream entity-alive guard covers the race window.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Containers/TripleBuffer.h"
#include "SkeletonTypes.h"
#include "FlecsCraftingSnapshot.h"
#include <atomic>
#include "FlecsCraftingUISubsystem.generated.h"

// ═══════════════════════════════════════════════════════════════
// SHARED MEMORY STRUCT (lock-free, subsystem owns)
// ═══════════════════════════════════════════════════════════════

/** Per-station shared state. Sim thread writes, game thread reads. Mirrors FContainerSharedState. */
struct FCraftingStationSharedState
{
	/** Sim → Game: latest station snapshot (lock-free triple buffer). */
	TTripleBuffer<FCraftingStationSnapshot> SnapshotBuffer;

	/** Sim → Game: version counter (1 atomic load for dirty check). */
	alignas(64) std::atomic<uint32> SimVersion{0};

	/** Game thread only: last seen version (no atomic needed). */
	uint32 GameSeenVersion = 0;

	/**
	 * Sim → Game (Phase 3): fast channel for the Smelter / Press / Forge process phase.
	 * Sim writes EVERY tick from SmelterProcessSystem (release ordering); game thread
	 * reads any time (acquire ordering). Init = 0 = EProcessPhase::Idle.
	 *
	 * Why a separate atomic instead of relying on SnapshotBuffer:
	 *  - Snapshot publish is THROTTLED (only on bSnapshotDirty ticks); cosmetic UI
	 *    that wants "is processing right now?" should NOT pay the snapshot rebuild.
	 *  - One byte atomic load is < 1 ns; widget Tick can poll it freely.
	 *  - Snapshot.ProcessPhase is the SAME data, eventually consistent within one tick;
	 *    use it for fields that must be coherent with Slots / ProgressSeconds.
	 */
	alignas(64) std::atomic<uint8> ProcessPhasePacked{0};
};

// ═══════════════════════════════════════════════════════════════
// SUBSYSTEM
// ═══════════════════════════════════════════════════════════════

UCLASS()
class FATUMGAME_API UFlecsCraftingUISubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// ═══ Singleton (mirrors UFlecsUISubsystem) ═══
	static inline UFlecsCraftingUISubsystem* SelfPtr = nullptr;

	/** Create the shared state for a station. Game thread only (called via AsyncTask from sim). */
	void CreateSharedState(FSkeletonKey StationKey);

	/** Destroy the shared state for a station. Game thread only (called via AsyncTask from sim). */
	void DestroySharedState(FSkeletonKey StationKey);

	/** Lookup shared state. Returns nullptr when station not yet registered or already destroyed.
	 *  Safe on game thread (reader) AND sim thread (reader) — mutations are game-thread-only
	 *  from Create/Destroy and occur only at station spawn/destroy (rare events). */
	FCraftingStationSharedState* FindSharedState(FSkeletonKey StationKey);

	// ═══ Tickable (widgets pull on their own tick — no per-frame work here) ═══
	virtual void Tick(float DeltaTime) override {}
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Subsystem-owned map. Mutations game-thread-exclusive; sim-thread reads via FindSharedState.
	 *  TUniquePtr ensures the TTripleBuffer + atomic have stable addresses across rehashes. */
	TMap<FSkeletonKey, TUniquePtr<FCraftingStationSharedState>> StationSharedStates;
};
