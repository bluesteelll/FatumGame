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
#include "Containers/Array.h"
#include "Containers/ContainerAllocationPolicies.h"
#include "UObject/ObjectPtr.h"
#include "FlecsCraftingTypes.h"

class UFlecsCraftingStationProfile;
class UFlecsCraftingRecipeDef;
class UFlecsFuelProfile;
class UFlecsEntityDefinition;

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
 *
 * V2 PATCH 1 (Phase 4): OwningPortIndex stamped at slot creation. 0xFF = baseline
 * (profile-defined) slot; 0..7 = extension port index. EnsurePortSlot uses this
 * for defense-in-depth identity check during RecomputeEffectiveLayout.
 */
struct FCraftingSlotBackRef
{
	int64   StationEntityId = 0;  // non-zero if this container is a crafting-station slot
	uint16  SlotIndex = 0;        // index into FCraftingSlots::SlotEntityIds
	uint8   Role = 0;             // ESlotRole narrowed to uint8
	uint8   OwningPortIndex = 0xFF; // 0..7 = extension port index, 0xFF = baseline slot (Phase 4)
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
// SMELTER (Phase 3) — process state + ingredient ledger + per-slot lock.
// All three are plain C++ structs, NOT USTRUCT. They live exclusively on the
// sim thread and are never marshalled across the thread boundary, so they
// follow the same convention as FCraftingStationStatic / FWeaponStatic.
// ═══════════════════════════════════════════════════════════════

/**
 * One row in FSmelterInstance::ConsumedLedger — captures a single ingredient that was
 * physically removed from a MaterialInput slot at Smelter Start. Used on Cancel to refund
 * the exact items back to their source slots (or floor-drop on overflow).
 *
 * Definition rooting: TObjectPtr<UFlecsEntityDefinition> is rooted via
 *   UFlecsCraftingRecipeRegistry::AllRecipes (UPROPERTY)
 *     → recipe.Ingredients[].ResolvedDefinition (TObjectPtr UPROPERTY)
 * chain — refactoring the registry must preserve the UPROPERTY anchor or the ledger
 * starts dangling. Phase 3 invariant: ledger lifetime <= MatchedRecipe lifetime,
 * which equals registry lifetime (game session).
 */
struct FConsumedIngredient
{
	TObjectPtr<UFlecsEntityDefinition>  Definition;          //  8 B
	int32                               Count            = 0; //  4 B
	uint8                               SourceSlotIndex  = 0; //  1 B
	// 3 B padding → 16 B total
};
static_assert(sizeof(FConsumedIngredient) == 16, "FConsumedIngredient unexpected size");

/**
 * Per-station Smelter state. Attached to Smelter station entities by
 * FlecsMultiblockRuntime::SetupStationInstance when Profile->StationType == Smelter.
 *
 * Owned EXCLUSIVELY by the sim thread. SmelterProcessSystem reads/writes Phase /
 * ProgressSeconds / DurationCached / ConsumedLedger; BP API command lambdas
 * (RequestSmelterStart / RequestSmelterCancel) flip bStartRequested / bCancelRequested
 * — both lambdas execute on the sim thread via EnqueueCommand. Game thread NEVER
 * touches this struct directly; it observes phase via FCraftingStationSharedState
 * atomic publish or the snapshot triple buffer.
 *
 * Inline allocator size 8 covers >99% of recipes (Phase 5 Press is the only foreseen
 * 8+ ingredient recipe; heap fallback handles the rare case correctly).
 */
struct FSmelterInstance
{
	EProcessPhase                                    Phase            = EProcessPhase::Idle;
	float                                            ProgressSeconds  = 0.f;
	float                                            DurationCached   = 0.f;   // copied from MatchedRecipe at Start
	TArray<FConsumedIngredient, TInlineAllocator<8>> ConsumedLedger;           // ingredient commit/refund snapshot
	bool                                             bStartRequested  = false; // set by BP API, drained by system
	bool                                             bCancelRequested = false; // set by BP API, drained by system
};

/**
 * Per-slot-container lock. Added to a slot CONTAINER entity (NOT the station) when
 * the owning station enters Processing/Stalled, removed when it returns to Idle.
 *
 * FlecsContainerLibrary's player-facing entry points (AddItemToContainer,
 * RemoveItemFromContainer, RemoveAllItemsFromContainer, TransferItem, PickupItem,
 * PickupWorldItem, PlaceExistingEntityInContainer) check this and reject the
 * mutation with a Warning when present. The sim-internal *FromStation siblings
 * (FlecsCraftingRuntime::AddItemToContainerFromStation /
 * RemoveItemFromContainerFromStation) bypass the check by design — the lock is
 * the player-vs-station discriminator.
 */
struct FCraftingSlotLockedByStation
{
	int64 OwningStationEntityId = 0;  // 0 = unlocked (defensive — typically the component is removed instead)
};

// ═══════════════════════════════════════════════════════════════
// PHASE 4 — STATION EFFECTIVE LAYOUT (derived state — extensions deltas)
// ═══════════════════════════════════════════════════════════════

/**
 * Derived station effective layout. Lives on station entities whose blueprint
 * has any ExtensionPort. Recomputed by RecomputeEffectiveLayout after attach /
 * detach / part death. NOT a snapshot — sim-thread state.
 *
 * Effective slots = profile baseline + sum of attached extension contributions.
 * Effective fuel ceiling = baseline (90s default) + 60s × #FuelTank extensions occupied.
 *
 * Snapshot reads BOTH FCraftingStationStatic.Profile (baseline) AND this (delta).
 */
struct FStationEffectiveLayout
{
	uint8  EffectiveSlotCount    = 0;   // total slots in FCraftingSlots after recompute
	float  EffectiveFuelCeiling  = 0.f; // ceiling on FuelChargeSecondsRemaining
	uint8  ExtensionPortsOccupiedCount = 0;
	uint16 MissingRequiredRolesBitmask = 0;  // bit i = role index → 1 if any required functional with that role is missing
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

/**
 * Phase 4 — transient one-tick tag. Added by TransitionStationToDisabled when
 * station enters Disabled, removed by TransitionStationToIdle when leaving.
 * Lets queries early-exit cheaply via .without<FTagStationDisabled>().
 * Source of truth for the Disabled state is FSmelterInstance.Phase.
 */
struct FTagStationDisabled {};
