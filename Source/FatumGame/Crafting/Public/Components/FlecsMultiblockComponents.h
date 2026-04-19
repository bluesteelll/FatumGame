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
 * Fixed-capacity list of bonded child entity ids, written by BondMultiblock at
 * bond time. ChildCount is the live child count — ids past it are garbage.
 *
 * Consumers must is_alive()-guard each id: children can die from damage while
 * bonded (R1) — Phase 3 deconstruct handles the cleanup.
 */
struct FMultiblockChildren
{
	int64 ChildEntityIds[15] = {};
	uint8 ChildCount = 0;
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
// TAGS (zero-size archetype markers)
// ═══════════════════════════════════════════════════════════════

/** Prefab-level: the entity type participates in multiblock assembly. */
struct FTagMultiblockPart {};

/** Prefab-level: the entity type is the anchor of its blueprint. */
struct FTagMultiblockAnchor {};

/** Instance-level: the entity has been bonded into an assembled multiblock. */
struct FTagMultiblockBonded {};
