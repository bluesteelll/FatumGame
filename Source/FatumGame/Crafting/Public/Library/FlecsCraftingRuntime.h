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
#include "Containers/Array.h"
#include "Containers/ContainerAllocationPolicies.h"
#include "FlecsCraftingTypes.h"
#include "FlecsCraftingSnapshot.h"
#include "Components/FlecsCraftingComponents.h"

namespace flecs { struct world; struct entity; }
class UFlecsCraftingRecipeDef;
class UFlecsCraftingRecipeRegistry;
class UFlecsCraftingStationProfile;
class UFlecsEntityDefinition;
struct FCraftingStationStatic;
struct FCraftingStationInstance;
struct FCraftingSlots;
struct FSmelterInstance;
struct FConsumedIngredient;

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

	// ═══════════════════════════════════════════════════════════════
	// SMELTER (Phase 3)
	// All sim-thread only. Mutating helpers early-exit when the station carries
	// FTagCraftingStationDestroying (consistent with Phase 1 helpers).
	// ═══════════════════════════════════════════════════════════════

	/** Bitmask describing which ESlotRole(s) participate in a station-process lock. */
	enum class ESlotRoleLockMask : uint8
	{
		MaterialInputOnly      = 1,
		MaterialInputAndFuel   = 2,
		All                    = 3,
	};

	/**
	 * Walk MaterialInput slots and build a one-shot ledger of (Definition, Count, SourceSlotIndex)
	 * tuples that fully satisfy Recipe->Ingredients. Multiple slots may contribute to a single
	 * ingredient (collected in slot-iteration order). Returns false when ANY ingredient cannot
	 * be fully satisfied (no partial mutation; caller bails the Start).
	 *
	 * Sim-thread only. Pure read — does not mutate slots / items.
	 */
	FATUMGAME_API bool BuildIngredientLedger(
		flecs::entity                                       Station,
		const FCraftingStationStatic&                       Static,
		const FCraftingSlots&                               Slots,
		const UFlecsCraftingRecipeDef*                      Recipe,
		TArray<FConsumedIngredient, TInlineAllocator<8>>&   OutLedger);

	/**
	 * Inspect the FuelSlot contents and sum projected charge seconds (Count × ChargeSecondsPerUnit)
	 * across items of the requested fuel type. Returns 0 when the slot is empty / mismatched.
	 *
	 * Sim-thread only. Pure read.
	 */
	FATUMGAME_API float ProbeFuelSlotProjectedCharge(flecs::entity Station, EFuelType RequiredType);

	/**
	 * Add FCraftingSlotLockedByStation { OwningStationEntityId } to each slot container whose
	 * role is included in the mask. Idempotent (re-adding overwrites). Sim-thread only.
	 */
	FATUMGAME_API void LockStationSlots(
		flecs::entity Station, const FCraftingSlots& Slots, ESlotRoleLockMask Mask);

	/** Symmetric — removes the lock component on each matching slot. Idempotent. Sim-thread only. */
	FATUMGAME_API void UnlockStationSlots(
		flecs::entity Station, const FCraftingSlots& Slots, ESlotRoleLockMask Mask);

	/**
	 * Spawn an item entity at the station's Barrage body position + Z lift via game-thread
	 * AsyncTask (UFlecsEntityLibrary::SpawnEntity is NOT sim-thread-safe — it touches Barrage
	 * + ISM on the game thread). Result item appears with one-tick latency (next game tick).
	 *
	 * Sim-thread only.
	 */
	FATUMGAME_API void DropOverflowToFloor(
		flecs::entity Station, UFlecsEntityDefinition* ItemDef, int32 Count);

	/**
	 * Sim-thread: scan a container for items matching ItemDef->ItemDefinition->ItemTypeId,
	 * decrement counts (largest stack first); destruct items at Count<=0; update container
	 * counters and dirty the station's snapshot. BYPASSES the station lock.
	 *
	 * @return true iff EXACTLY Count items were removed. On false return, partial removal
	 *         may have occurred — caller is responsible for rollback decisions.
	 */
	FATUMGAME_API bool RemoveExactCountFromContainerFromStation(
		flecs::entity Station,
		int64 ContainerEntityId,
		UFlecsEntityDefinition* ItemDef,
		int32 Count);

	// ── Smelter state-machine entry points (called from SmelterProcessSystem). ──

	/** Idle → (Processing OR rejected). Validates recipe + ingredients + fuel projection,
	 *  builds ledger, removes items, locks slots. */
	FATUMGAME_API void TrySmelterStart(
		flecs::entity Station,
		const FCraftingStationStatic& Static,
		FCraftingStationInstance& Inst,
		FSmelterInstance& SmInst,
		const FCraftingSlots& Slots);

	/** Processing per-tick: spend fuel, advance progress, transition to Stalled / Completing. */
	FATUMGAME_API void SmelterTickProcessing(
		flecs::entity Station,
		const FCraftingStationStatic& Static,
		FCraftingStationInstance& Inst,
		FSmelterInstance& SmInst,
		float DT);

	/** Stalled per-tick: try to consume a fuel item; resume Processing on success. */
	FATUMGAME_API void SmelterTickStalled(
		flecs::entity Station,
		FCraftingStationInstance& Inst,
		FSmelterInstance& SmInst);

	/** Cancel branch — refund ledger items, reset progress; reservoir UNCHANGED. Phase=Cancelled. */
	FATUMGAME_API void SmelterCancel(
		flecs::entity Station,
		const FCraftingStationStatic& Static,
		FCraftingStationInstance& Inst,
		FSmelterInstance& SmInst,
		const FCraftingSlots& Slots);

	/** Cancelled → Idle. Removes lock, dirty snapshot. */
	FATUMGAME_API void SmelterFinalizeCancel(
		flecs::entity Station,
		FCraftingStationInstance& Inst,
		FSmelterInstance& SmInst,
		const FCraftingSlots& Slots);

	/** Completing → Idle. Emits outputs (slot or floor drop), clears ledger, removes lock. */
	FATUMGAME_API void SmelterComplete(
		flecs::entity Station,
		const FCraftingStationStatic& Static,
		FCraftingStationInstance& Inst,
		FSmelterInstance& SmInst,
		const FCraftingSlots& Slots);
}
