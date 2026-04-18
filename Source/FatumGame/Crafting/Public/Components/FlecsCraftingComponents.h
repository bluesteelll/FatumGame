// Crafting ECS components — plain C++ structs (no GENERATED_BODY), mirroring
// FWeaponStatic / FMeleeWeaponStatic convention.
//
// Split:
//   FCraftingStationStatic           — prefab-level shared data (station type, profile ref)
//   FCraftingStationInstance         — per-entity mutable state (matched recipe, fuel, dirty flag)
//   FCraftingSlots                   — per-station, 24-slot table of child container entity ids
//   FFuelSlot                        — per-station, denormalized fuel slot entity id
//   FCraftingSlotBackRef             — lives on each SLOT CONTAINER entity (observer fast-gate)
//   FCraftingFuelItemData            — lives on FUEL ITEM prefabs (QU4 renamed from FCraftingFuelItemStatic)
//
// Tags:
//   FTagCraftingStation              — marks station entities for queries
//   FTagCraftingStationDestroying    — suppresses observer/library hooks during teardown
//   FTagCraftingFuel                 — marks fuel-item prefabs
//
// SNAPSHOT CHANNEL LIVES OFF ECS (MJ3) — see FlecsCraftingUISubsystem.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "FlecsCraftingTypes.h"

class UFlecsCraftingStationProfile;
class UFlecsCraftingRecipeDef;
class UFlecsFuelProfile;

// ═══════════════════════════════════════════════════════════════
// STATION STATIC (prefab-level)
// ═══════════════════════════════════════════════════════════════

/**
 * Shared per station TYPE (prefab).
 *
 * Holds only the back-pointer to the profile — systems dereference
 * Profile->SlotLayout on demand (MN3 resolution for MN3/TArray-in-plain-struct).
 */
struct FCraftingStationStatic
{
	ECraftingStationType                   StationType = ECraftingStationType::Generic;

	/** Bitmask of EFuelType values the station accepts. Must be non-zero iff layout has a Fuel slot. */
	uint16                                 SupportedFuelMask = 0;

	/** Debug/editor label. */
	FName                                  StationName;

	/** Gameplay tag for recipe gating (e.g. Station.Smelter). */
	FGameplayTag                           StationTag;

	/** Profile back-ref — OWNS SlotLayout. Pointer lifetime coupled to station prefab. */
	const UFlecsCraftingStationProfile*    Profile = nullptr;

	/** Validate + populate from profile. Asserts slot-layout rules via checkf. */
	static FCraftingStationStatic FromProfile(const UFlecsCraftingStationProfile* P);
};

// ═══════════════════════════════════════════════════════════════
// STATION INSTANCE (per-entity, sim-thread-owned)
// ═══════════════════════════════════════════════════════════════

/**
 * Per-entity mutable crafting state.
 * ONLY written by sim-thread systems + observer (bSnapshotDirty boolean flip).
 * Observer NEVER mutates MatchedRecipe, digests, or fuel fields — those are flush-system territory.
 */
struct FCraftingStationInstance
{
	/** Last successful recipe match. Hard pointer rooted by registry (Phase 1 game lifetime). */
	const UFlecsCraftingRecipeDef*         MatchedRecipe = nullptr;

	/** Digest of current slot contents + active fuel. Bumped when slots or fuel mutate. */
	uint64                                 SlotDirtyDigest = 0;

	/** Digest at which MatchRecipe was last invoked — skip recompute when equal. */
	uint64                                 LastMatchedDigest = 0;

	/** Diagnostic from last MatchRecipe call — published in snapshot for UI. */
	ECraftingMatchDiagnostic               LastDiagnostic = ECraftingMatchDiagnostic::None;

	/** Fuel reservoir in seconds. `FuelChargeSecondsRemaining > 0 ⇒ ActiveFuelType != None` (invariant). */
	float                                  FuelChargeSecondsRemaining = 0.f;

	/** Which fuel type the reservoir was filled with. */
	EFuelType                              ActiveFuelType = EFuelType::None;

	/**
	 * Flush-system coalescing flag.
	 * SET by: observer (FContainedIn OnSet), library hooks (TransferItem, Remove*), spawner (initial true).
	 * CLEARED by: CraftingSnapshotFlushSystem ONLY, after PushSnapshot completes.
	 */
	bool                                   bSnapshotDirty = true;
};

// ═══════════════════════════════════════════════════════════════
// SLOT TABLE
// ═══════════════════════════════════════════════════════════════

/**
 * Fixed-capacity slot index per station.
 *
 * SlotEntityIds[i] = flecs::entity id of the container entity backing slot i.
 *                     0 means unused (layout has fewer than kMaxCraftingSlots slots).
 */
struct FCraftingSlots
{
	int64   SlotEntityIds[kMaxCraftingSlots] = {};
	uint8   SlotRoleCounts[static_cast<uint8>(ESlotRole::MAX)] = {};

	FCraftingSlots()
	{
		for (int32 i = 0; i < kMaxCraftingSlots; ++i) SlotEntityIds[i] = 0;
		for (int32 i = 0; i < static_cast<int32>(ESlotRole::MAX); ++i) SlotRoleCounts[i] = 0;
	}

	/** Return 0-indexed N-th slot entity id of the given role. Returns 0 when out of range. */
	int64 GetByRole(ESlotRole Role, int32 NthIndex = 0) const;

	/** First free (zero) index, or INDEX_NONE if full. */
	int32 FindFreeIndex() const;
};

// ═══════════════════════════════════════════════════════════════
// FUEL SLOT (denormalized lookup for the fuel-slot container entity)
// ═══════════════════════════════════════════════════════════════

struct FFuelSlot
{
	int64 FuelSlotEntityId = 0;  // 0 if station has no fuel slot
};

// ═══════════════════════════════════════════════════════════════
// SLOT CONTAINER BACK-REFERENCE
// ═══════════════════════════════════════════════════════════════

/**
 * Lives on each SLOT CONTAINER entity (not the station).
 * Lets the FContainedIn observer fast-gate: if this component is absent, the
 * container is not a crafting slot and the observer early-exits.
 */
struct FCraftingSlotBackRef
{
	int64   StationEntityId = 0;  // non-zero if this container is a crafting-station slot
	uint16  SlotIndex = 0;        // index into FCraftingSlots::SlotEntityIds
	uint8   Role = 0;             // ESlotRole narrowed to uint8
};

// ═══════════════════════════════════════════════════════════════
// FUEL ITEM DATA (QU4 — renamed from FCraftingFuelItemStatic)
// Lives on fuel-item PREFABS, inherited by every spawned instance.
// ═══════════════════════════════════════════════════════════════

struct FCraftingFuelItemData
{
	EFuelType   FuelType = EFuelType::None;
	float       ChargeSecondsPerUnit = 0.f;

	static FCraftingFuelItemData FromProfile(const UFlecsFuelProfile* P);
};

// ═══════════════════════════════════════════════════════════════
// TAGS (zero-size, archetype markers)
// ═══════════════════════════════════════════════════════════════

/** Marks an entity as a crafting station (queried by flush system). */
struct FTagCraftingStation {};

/** Marks a station mid-teardown — observer + library hooks must early-exit. */
struct FTagCraftingStationDestroying {};

/** Marks fuel-item prefabs — lets SlotFilter / UI / matching quickly identify fuel. */
struct FTagCraftingFuel {};
