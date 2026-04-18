// Sim-thread-only crafting runtime helpers.
//
// Namespace, not a UObject. All functions assume sim-thread reentry guarantees provided
// by UFlecsArtillerySubsystem::EnqueueCommand / system-phase execution. NEVER call these
// from game thread directly — route through UFlecsCraftingLibrary.
//
// Every mutating call checks FTagCraftingStationDestroying at entry and early-returns
// so in-flight commands issued before destroy do not revive dead state.

#pragma once

#include "CoreMinimal.h"
#include "FlecsCraftingTypes.h"
#include "FlecsCraftingSnapshot.h"

namespace flecs { struct world; struct entity; }
class UFlecsCraftingRecipeDef;
class UFlecsCraftingRecipeRegistry;

namespace FlecsCraftingRuntime
{
	// ═══════════════════════════════════════════════════════════════
	// DIRTY FLAG
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Mark a station's FCraftingStationInstance::bSnapshotDirty = true.
	 * No-op if station lacks the instance or carries FTagCraftingStationDestroying.
	 * Sim-thread only.
	 */
	FATUMGAME_API void InvalidateMatchCache(flecs::entity StationE);

	/**
	 * Convenience wrapper used by library hooks: both the source and destination
	 * containers may belong to crafting stations (cross-station TransferItem case),
	 * so dirty both owners when present. Either id may be 0 to skip that side.
	 * Sim-thread only.
	 */
	FATUMGAME_API void MarkStationDirtyByContainer(
		flecs::world& World,
		int64 OldContainerId,
		int64 NewContainerId);

	// ═══════════════════════════════════════════════════════════════
	// DIGEST + MATCH
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Compute a uint64 digest over all MaterialInput + Fuel slot contents plus the
	 * active fuel byte. Equal digests imply identical inputs — match can be skipped.
	 * Sim-thread only.
	 */
	FATUMGAME_API uint64 ComputeSlotDigest(flecs::entity StationE);

	/**
	 * Run a registry bucket search and update FCraftingStationInstance::MatchedRecipe /
	 * LastDiagnostic. Caller is expected to own LastMatchedDigest bookkeeping after this
	 * returns (see flush-system pseudocode in blueprint §1.5.3).
	 *
	 * Resets LastDiagnostic to None unconditionally at entry (MN5 — no stale carry).
	 * Returns the matched recipe (may be null).
	 * Sim-thread only.
	 */
	FATUMGAME_API const UFlecsCraftingRecipeDef* MatchRecipe(
		flecs::entity StationE,
		UFlecsCraftingRecipeRegistry* Registry);

	// ═══════════════════════════════════════════════════════════════
	// FUEL
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Infer the active fuel classification for a station.
	 * Priority:
	 *   1. If FuelChargeSecondsRemaining > 0 → return FCraftingStationInstance::ActiveFuelType.
	 *   2. Otherwise inspect first item in fuel slot → return its FCraftingFuelItemData.FuelType.
	 *   3. Otherwise EFuelType::None.
	 *
	 * Sim-thread only.
	 */
	FATUMGAME_API EFuelType ResolveFuelType(flecs::entity StationE);

	/**
	 * Apply the fuel-slot state machine once (see blueprint §1.5.3).
	 *
	 * Consumes one fuel item per call when reservoir hits zero; refuses to mix fuel types;
	 * logs a single warning per mismatched-type attempt per station per session. Does not
	 * drain the reservoir — that is Phase 3 / SmelterProcessSystem's job.
	 *
	 * @param AmountSeconds  Reserved for Phase 3 (drain step). Phase 1 ignores.
	 * @return true when any state change occurred (new item consumed, reservoir refilled).
	 *
	 * MUST be called from the flush system only. NEVER from an observer body.
	 * Sim-thread only.
	 */
	FATUMGAME_API bool TryConsumeFuelSlot(flecs::entity StationE, float AmountSeconds = 0.f);

	// ═══════════════════════════════════════════════════════════════
	// SNAPSHOT
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Populate OutSnapshot from the station's current ECS state.
	 * Reads FCraftingStationStatic + Instance + Slots, enumerates FContainedIn per slot.
	 * Does NOT push to any triple buffer — Step 11 flush system handles that.
	 * Sim-thread only.
	 */
	FATUMGAME_API void BuildSnapshot(flecs::entity StationE, FCraftingStationSnapshot& OutSnapshot);
}
