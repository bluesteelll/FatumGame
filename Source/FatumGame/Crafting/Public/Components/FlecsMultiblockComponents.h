// Multiblock assembly ECS components — Phase 2 of the Crafting System.
//
// See CRAFTING_PHASE2_BLUEPRINT_V2.md / V3.md for the design rationale.
// Summary: parts spawn as normal pickupable entities with FMultiblockPartStatic;
// a periodic detection system scans for anchors, matches required children by
// spatial layout + role, and (on match) bonds the ensemble — stripping pickup
// tags, freezing bodies to Static, and upgrading the anchor into a crafting
// station via FlecsMultiblockRuntime::SetupStationInstance.
//
// All members are plain C++ structs (no GENERATED_BODY) — registered as Flecs
// components in FlecsArtillerySubsystem_Systems.cpp::RegisterFlecsComponents.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NameTypes.h"

class UFlecsMultiblockBlueprint;

// ═══════════════════════════════════════════════════════════════
// PART STATIC (prefab-level)
// ═══════════════════════════════════════════════════════════════

/**
 * Shared metadata per part TYPE (prefab).
 *
 * Blueprint pointer is raw — GC-rooted indirectly via
 * UFlecsEntityDefinition::MultiblockBlueprint (TObjectPtr).
 *
 * Anchor status is encoded as the FTagMultiblockAnchor tag (not a bool here),
 * to avoid a dual source of truth (v3 PATCH 3 / critique M1).
 */
struct FMultiblockPartStatic
{
	const UFlecsMultiblockBlueprint* Blueprint = nullptr;
	FName                            PartRole;
};

// ═══════════════════════════════════════════════════════════════
// ANCHOR ROSTER (per-entity, anchor side only)
// ═══════════════════════════════════════════════════════════════

/**
 * Per-slot row inside FMultiblockChildren. Replaces the flat int64 array used
 * in Phase 2 — Phase 4 needs per-child swappability + extension-port linkage.
 *
 * Lives INLINE inside FMultiblockChildren — NOT a Flecs component itself
 * (mirrors FConsumedIngredient pattern).
 */
struct FMultiblockChildSlot
{
	int64  ChildEntityId    = 0;     // 8 B  Flecs entity id of bonded child (0 = vacant slot)
	FName  PartRole;                  // 8 B  cached from FMultiblockChildPartSpec::PartRole
	uint8  bSwappable       = 0;     // 1 B  0 = rigid, 1 = hot-swappable via wrench
	uint8  bIsExtension     = 0;     // 1 B  0 = required child, 1 = extension port
	uint8  PortIndex        = 0;     // 1 B  index into Blueprint->ExtensionPorts when bIsExtension
	uint8  _pad             = 0;     // 1 B  align
};

/**
 * Fixed-capacity list of bonded child slot rows, written by BondMultiblock at
 * bond time. ChildCount is the live child count.
 *
 * AnchorYawSnappedDeg is written ONCE at bond time (Phase 4 R3 closure):
 * snapped to {0, 90, 180, 270}; detection / attach use this for rotated offsets.
 */
struct FMultiblockChildren
{
	FMultiblockChildSlot ChildSlots[15] = {};
	uint8                ChildCount     = 0;
	uint16               AnchorYawSnappedDeg = 0;  // {0, 90, 180, 270} — written once at bond
};

// ═══════════════════════════════════════════════════════════════
// CHILD BACK-REFERENCE (per-entity, child side only)
// ═══════════════════════════════════════════════════════════════

/**
 * Lives on each bonded CHILD entity.
 *
 * v3 PATCH 1 / critique C1: PreBondMotionType dropped — UBarrageDispatch has no
 * GetBodyMotionType() getter, so we cannot record it. Phase 3 deconstruct will
 * restore all parts to Dynamic unconditionally (R3/R6: authoring contract says
 * parts must originate as MOVING/Dynamic).
 */
struct FMultiblockChildOf
{
	int64 AnchorEntityId = 0;
};

// ═══════════════════════════════════════════════════════════════
// PHASE 4 — MODULAR EXTENSIONS (per-station roster of occupied ports)
// ═══════════════════════════════════════════════════════════════

/**
 * Per-station roster of currently-occupied extension ports. Index = port index
 * in Blueprint->ExtensionPorts. Value = entity id of attached part (0 = empty).
 *
 * Lives ONLY on stations whose blueprint has at least one ExtensionPort.
 * SetupStationInstance writes empty (zero) when station has any ExtensionPorts.
 * AttachPartToStation / DetachPartFromStation update.
 */
struct FMultiblockExtensions
{
	int64 PortOccupants[8] = {};   // 8 ports max (designer cap, IsDataValid)
	uint8 PortCount        = 0;    // copied from Blueprint->ExtensionPorts.Num()
};

/**
 * Lives on a part item entity that is mid-attach. Written by AttachPartToStation
 * at command-lambda entry, removed by FinalizePartAttach on success / by
 * ClearStaleAttachReservationsSystem 5-tick timeout. Prevents player from
 * dragging the part out of inventory while sim is mid-spawn-and-bond.
 */
struct FPendingPartAttach
{
	int64  TargetStationEntityId = 0;
	uint64 EnqueuedTickStamp     = 0;  // sim tick when added; for 5-tick timeout
};

/**
 * Lives on a STATION while ANY port has an in-flight attach (between
 * AttachPartToStation entry and FinalizePartAttach completion / 5-tick timeout).
 * Carries the reserved port index — second concurrent attach to ANY port on
 * this station rejects with Verbose log (V2 PATCH 3 idempotency guard).
 *
 * Rejection is INTENTIONAL: serialize attach operations per-station to avoid
 * the spawn marshal race. The 1-3 tick latency is imperceptible; a held E
 * re-fires next tick.
 */
struct FPendingStationAttach
{
	int32  ReservedPortIndex   = INDEX_NONE;
	uint64 EnqueuedTickStamp   = 0;
};

// ═══════════════════════════════════════════════════════════════
// TAGS (zero-size archetype markers)
// ═══════════════════════════════════════════════════════════════

/** Prefab-level: the entity type participates in multiblock assembly. */
struct FTagMultiblockPart {};

/** Prefab-level: the entity type is the anchor of its blueprint. */
struct FTagMultiblockAnchor {};

/** Instance-level: the entity has been bonded into an assembled multiblock. */
struct FTagMultiblockBonded {};

/** Lives on items tagged as wrenches. Read by game thread to gate interaction routing. */
struct FTagWrench {};
