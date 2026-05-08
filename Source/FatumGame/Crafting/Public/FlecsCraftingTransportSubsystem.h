// UFlecsCraftingTransportSubsystem — Phase 5a transport graph roster owner.
//
// Mirrors UFlecsCraftingUISubsystem singleton/lifecycle pattern. The roster
// (`SimNetworkRoster`) is owned by the sim thread — only NetworkRebuildSystem
// mutates it. Game thread reads via DumpNetworks for debug only (5a). Real
// cosmetic snapshot publication is 5d.
//
// Cite: V1 §1.3 subsystem-owned struct; V2 PATCH 8 BFS implementation guidance;
// Phase 1 UFlecsCraftingUISubsystem singleton/init pattern.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Containers/Map.h"
#include "FlecsCraftingTypes.h"
#include "FlecsCraftingTransportSubsystem.generated.h"

namespace flecs { struct world; }

// ═══════════════════════════════════════════════════════════════
// SUBSYSTEM-OWNED POD (NOT a Flecs component)
// ═══════════════════════════════════════════════════════════════

/**
 * Phase 5a — one entry per connected component in the transport graph.
 *
 * NetworkId is a PER-REBUILD EPOCH, NOT a persistent identifier. NextNetworkId is
 * reset to 1 on every rebuild — any system caching a NetworkId across ticks must
 * re-resolve via FindNetwork() and accept null. 5a has no such cachers; 5b's
 * FPowerRequest.NetworkId will need same-tick re-validation per V2 PATCH 3.
 *
 * Inline allocators: typical scene ≤8 stations per network, ≤64 segments per network.
 * SegmentIds is the hot path (BFS append); StationIds + Outlet/InletStationIds are
 * cold-path scheduling lookups (5b+).
 */
struct FCraftingTransportNetwork
{
	uint32         NetworkId = 0;
	ETransportKind Kind      = ETransportKind::None;
	TArray<int64, TInlineAllocator<64>> SegmentIds;
	TArray<int64, TInlineAllocator<8>>  StationIds;
	TArray<int64, TInlineAllocator<8>>  OutletStationIds;
	TArray<int64, TInlineAllocator<8>>  InletStationIds;
};

// ═══════════════════════════════════════════════════════════════
// SUBSYSTEM
// ═══════════════════════════════════════════════════════════════

UCLASS()
class FATUMGAME_API UFlecsCraftingTransportSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Singleton — set in Initialize, cleared in Deinitialize. Mirrors UFlecsCraftingUISubsystem. */
	static inline UFlecsCraftingTransportSubsystem* SelfPtr = nullptr;

	// ═══ Sim-thread API (writes) ═══════════════════════════════

	/** Set bRebuildPending atomic. Safe from any sim-thread system. */
	void QueueRebuild();

	/** Atomic check-and-clear. Returns true iff a rebuild was pending. Used by NetworkRebuildSystem. */
	bool ConsumeRebuildRequest();

	/** Full BFS rebuild — replaces SimNetworkRoster, bumps NetworkVersion.
	 *  Sim thread only. Called from NetworkRebuildSystem. */
	void RebuildAndPublish(flecs::world& World);

	// ═══ Sim-thread reads (inside other systems) ═══════════════

	/** Lookup network by id. Returns nullptr if not found. Sim thread only. */
	const FCraftingTransportNetwork* FindNetwork(uint32 NetworkId) const;

	/** Total network count. Sim thread only. */
	int32 GetNetworkCount() const { return SimNetworkRoster.Num(); }

	// ═══ Game-thread reads (debug only — 5a) ═══════════════════

	/** Dump roster to LogCrafting Display. Game-thread console hook.
	 *  RACE: SimNetworkRoster mutation is sim-thread; reading on game thread without
	 *  a lock is technically a race. Acceptable for 5a debug-only. Real cosmetic
	 *  snapshot is 5d. Logs a header line warning of this. */
	void DumpNetworks() const;

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Sim-thread-owned roster — mutated ONLY by NetworkRebuildSystem. */
	TMap<uint32, FCraftingTransportNetwork> SimNetworkRoster;

	/** Reset to 1 per rebuild — see FCraftingTransportNetwork doc-comment. */
	uint32 NextNetworkId = 1;

	/** Atomic edit-request flag. Set by editing systems; cleared by NetworkRebuildSystem. */
	std::atomic<bool> bRebuildPending{false};

	/** Published version — game-thread debug dumps may read for staleness check. */
	alignas(64) std::atomic<uint32> NetworkVersion{0};
};
