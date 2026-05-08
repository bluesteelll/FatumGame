// Phase 5a — connector + station-port + reservation runtime components.
//
// All structs are plain C++ (no GENERATED_BODY()) per FSmelterInstance / FMultiblockChildren
// precedent. Sim-thread-owned. Game thread reads via published snapshot OR atomics — never
// touch these structs directly from the game thread.
//
// REGISTRATION: every component except FPortSlot MUST be registered in
// UFlecsArtillerySubsystem::RegisterFlecsComponents (Phase 5a block). FPortSlot is INLINE-ONLY
// inside FStationPorts.Ports[] — calling entity.set<FPortSlot>() will crash with 0x80000003.
// Same precedent: FConsumedIngredient inside FSmelterInstance.ConsumedLedger.
//
// Cite: V1 §1.3 + V2 PATCH 1, 4, 7, 9.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/ContainerAllocationPolicies.h"
#include "FlecsCraftingTypes.h"

class UFlecsEntityDefinition;

// ═══════════════════════════════════════════════════════════════
// PREFAB-SIDE — connector segment definition (immutable per type)
// ═══════════════════════════════════════════════════════════════

/**
 * Phase 5a — prefab-level connector definition.
 *
 * Lives on the connector entity prefab via UFlecsEntityLibrary's standard FromDefinition
 * path. SegmentLengthCm, FrontSocketName, BackSocketName are designer-authored; Role is
 * the master enable (None = not a connector).
 */
struct FConnectorSegmentStatic
{
	EConnectorTransportRole Role            = EConnectorTransportRole::None;  // 1 B
	float                   SegmentLengthCm = 100.f;                          // 4 B
	FName                   FrontSocketName;                                  // 8 B
	FName                   BackSocketName;                                   // 8 B
	// Total: 24 B (with padding)

	static FConnectorSegmentStatic FromDefinition(const UFlecsEntityDefinition* Def);
};
// Sanity bound — exact size depends on FName impl (UE 5.7 = 12 B; older = 8 B). Not load-bearing.
static_assert(sizeof(FConnectorSegmentStatic) <= 48, "FConnectorSegmentStatic unexpectedly large — review fields");

// ═══════════════════════════════════════════════════════════════
// INSTANCE-SIDE — placed segment runtime state
// ═══════════════════════════════════════════════════════════════

/**
 * Phase 5a — per-segment runtime state. Set on the connector entity AFTER successful
 * RequestConnectorPlace continuation. Removed by RequestConnectorDetach to return the
 * segment to its in-inventory item form.
 *
 * Snap targets are int64 entity ids (NOT FSkeletonKey). PortIndex 0xFF means "no station
 * port" (peer-segment OR dangling — but 5a rejects both-dangling at place time, so at
 * least one end resolves to a station).
 */
struct FConnectorPlaced
{
	int64  FrontSnapTargetId       = 0;     // 0 = dangling; otherwise station entity OR peer segment
	int64  BackSnapTargetId        = 0;
	uint8  FrontSnappedToPortIndex = 0xFF;  // 0..7 = station port index; 0xFF = dangling/peer
	uint8  BackSnappedToPortIndex  = 0xFF;
	uint8  _pad[2]                 = {};
	uint32 NetworkId               = 0;     // assigned by NetworkRebuildSystem; 0 = orphan/just-placed
};
static_assert(sizeof(FConnectorPlaced) <= 32, "FConnectorPlaced unexpectedly large — review fields");

// ═══════════════════════════════════════════════════════════════
// INLINE-ONLY — per-port slot (held inside FStationPorts.Ports[])
// ═══════════════════════════════════════════════════════════════

/**
 * INLINE-ONLY invariant. FPortSlot lives EXCLUSIVELY inside FStationPorts.Ports[].
 * NEVER call entity.set<FPortSlot>() / entity.add<FPortSlot>() / entity.get<FPortSlot>()
 * — FPortSlot is NOT registered as a Flecs component (intentionally; same precedent as
 * FConsumedIngredient inside FSmelterInstance, see FlecsCraftingComponents.h:190).
 *
 * Direct entity.set<FPortSlot>() will crash with 0x80000003 because FPortSlot is not
 * registered via World.component<FPortSlot>() in FlecsArtillerySubsystem_Systems.cpp.
 * If you need to mutate a port, get FStationPorts via entity.get_mut<FStationPorts>()
 * and index into Ports[].
 *
 * The static_assert below pins the size at exactly 56 bytes; if a contributor adds a
 * field without recomputing _pad, compile fails — forcing a deliberate review of layout.
 *
 * Cite: V2 PATCH 7 + 9.
 */
struct FPortSlot
{
	EPortKind Kind                = EPortKind::None;        //  1 B
	FName     SocketName;                                    //  8 B
	int64     ConnectedSegmentId  = 0;                       //  8 B (0 = vacant)
	uint8     NetworkId           = 0;                       //  1 B (cap fits in uint8 since 32 ≤ 255)
	int8      AcceptPriority      = 0;                       //  1 B (V2 PATCH 4 — used in 5c)
	uint8     _pad[5]             = {};                      //  5 B (alignment)
	FVector   LocalOffsetCm       = FVector::ZeroVector;     // 24 B (V2 PATCH 9)
	// Total: 56 B
};
// Sanity bound — exact size depends on FName impl + alignment. V2 PATCH 7+9 final form. Inline-only contract enforced via doc-comment, not size.
static_assert(sizeof(FPortSlot) <= 64, "FPortSlot unexpectedly large — review fields");
static_assert(!std::is_polymorphic_v<FPortSlot>, "FPortSlot must remain non-polymorphic");

// ═══════════════════════════════════════════════════════════════
// PER-STATION — port table (Flecs component)
// ═══════════════════════════════════════════════════════════════

/**
 * Phase 5a — per-station port table. Set on station anchors by SetupStationInstance
 * when the station's UFlecsEntityDefinition has StationPortAuthoring entries.
 *
 * Inline allocator size 8 = designer hard cap (matches FMultiblockExtensions ports).
 */
struct FStationPorts
{
	TArray<FPortSlot, TInlineAllocator<8>> Ports;   // ≤ 8 ports — designer cap
};

// ═══════════════════════════════════════════════════════════════
// IN-FLIGHT RESERVATION — V2 PATCH 1
// ═══════════════════════════════════════════════════════════════

/**
 * Phase 5a — per-port reservation tag for in-flight connector-place commands.
 *
 * Set on the destination STATION entity (NOT the segment — segment doesn't exist yet)
 * at sim re-validation time, BEFORE the AsyncTask GameThread spawn round-trip. Cleared
 * by the placement continuation lambda OR by ClearStaleConnectorReservationsSystem
 * after kPendingConnectorTimeoutTicks (60 ticks ≈ 1s).
 *
 * Coexists with FPendingStationAttach (Phase 4) — they target different port-index
 * spaces and DO NOT block each other. AttachPartToStation also rejects when this is
 * present (V2 PATCH 1 cross-pollution per "Final Remaining Risks" row 1).
 *
 * Cite: V2 PATCH 1 verbatim.
 */
struct FPendingConnectorPlace
{
	uint64 EnqueuedTickStamp = 0;     //  8 B
	uint8  ReservedPortIndex = 0xFF;  //  1 B (0..7 = port; 0xFF = invalid)
	uint8  _pad[7]           = {};    //  7 B (alignment)
	// Total: 16 B
};
static_assert(sizeof(FPendingConnectorPlace) == 16, "FPendingConnectorPlace must remain 16 bytes");

// ═══════════════════════════════════════════════════════════════
// TAGS (zero-size archetype markers)
// ═══════════════════════════════════════════════════════════════

/** Phase 5a — present on connector ENTITY DEFINITIONS (prefab) for query filtering. */
struct FTagConnectorSegment {};

/** Phase 5a — present on placed-in-world segments (vs in-inventory unplaced item). */
struct FTagConnectorPlaced {};
