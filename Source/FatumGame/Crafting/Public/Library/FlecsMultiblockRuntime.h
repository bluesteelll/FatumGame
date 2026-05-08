// Sim-thread-only multiblock runtime helpers (Phase 2 + Phase 4 of the Crafting System).
//
// Namespace, not a UObject. All functions assume sim-thread reentry — safe to
// call from EnqueueCommand lambdas, Flecs systems, or spawner code after the
// entity has been bound to Barrage. NEVER call from game thread directly.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "FlecsCraftingTypes.h"
#include "SkeletonTypes.h"

namespace flecs { struct entity; }
class UFlecsCraftingStationProfile;
class UFlecsContainerProfile;
class UFlecsMultiblockBlueprint;
class UWorld;

// Global-scope forward decls — MUST be outside the FlecsMultiblockRuntime namespace
// so function signatures below reference the global types, not namespace-scoped ones
// (inline `struct FCraftingSlots*` parameter creates FlecsMultiblockRuntime::FCraftingSlots
// → use-of-undefined-type cascade in callers).
struct FCraftingSlots;

namespace FlecsMultiblockRuntime
{
	/**
	 * Attach crafting-station machinery to an existing live anchor entity.
	 *
	 * Spawns one child container entity per FSlotLayoutDef in the profile,
	 * tags each with FCraftingSlotBackRef, writes FCraftingSlots / FFuelSlot /
	 * FCraftingStationInstance to the anchor, and registers a UI shared-state
	 * bucket via AsyncTask on the game thread.
	 *
	 * Callers:
	 *   - FlecsEntitySpawner (Phase 1 — direct station spawn, no multiblock).
	 *   - BondMultiblock (Phase 2 — on successful assembly detection).
	 *
	 * Phase 4 — additionally writes empty FMultiblockExtensions + FStationEffectiveLayout
	 * if the bonded blueprint has any extension ports.
	 *
	 * Preconditions (enforced by check/checkf):
	 *   - Anchor is valid + alive.
	 *   - Profile is non-null.
	 *   - Anchor is NOT already a crafting station (FTagCraftingStation must
	 *     be absent — caller gates to avoid double-setup).
	 *
	 * Sim thread only.
	 */
	FATUMGAME_API void SetupStationInstance(
		flecs::entity Anchor,
		const UFlecsCraftingStationProfile* Profile);

	/**
	 * Phase 4 — Step 0 refactor. Spawn one slot container entity from a profile.
	 * Returns the container entity. Caller is responsible for setting
	 * FCraftingSlotBackRef on it. Used by SetupStationInstance and EnsurePortSlot.
	 *
	 * Sim thread only.
	 */
	FATUMGAME_API flecs::entity SpawnSlotContainerFromProfile(
		flecs::entity StationE,
		const UFlecsContainerProfile* SlotProfile,
		ESlotRole Role);

	// ═══════════════════════════════════════════════════════════════
	// PHASE 4 — MODULAR STATIONS (attach / detach / deconstruct / recompute)
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Resolve the multiblock blueprint for a station entity. The anchor entity
	 * itself carries FMultiblockPartStatic::Blueprint (set on the prefab).
	 * Returns nullptr if the station is not a multiblock-bonded entity.
	 */
	FATUMGAME_API const UFlecsMultiblockBlueprint* ResolveBlueprintForStation(flecs::entity StationE);

	/**
	 * Read the station's world position + rotation (anchor body). Returns false
	 * if the body is missing.
	 */
	FATUMGAME_API bool ReadStationWorldTransform(
		flecs::entity StationE, FVector& OutPos, FQuat& OutQuat);

	/**
	 * Restore a part body to Dynamic (inverse of BondMultiblock Step 2 freeze).
	 * Calls SetBodyMotionType(Dynamic, Activate=true) via Barrage.
	 */
	FATUMGAME_API void RestorePartToDynamic(flecs::entity ChildE);

	/**
	 * Apply a small Z+ impulse so a detached part visibly pops off and falls.
	 * Magnitude designer-tunable via UFlecsCraftingStationProfile::DeconstructImpulseCmS.
	 */
	FATUMGAME_API void ApplyDetachImpulse(flecs::entity ChildE, float ImpulseCmS);

	/**
	 * Drain + destruct one slot's items + the slot container entity itself.
	 * Caller is expected to clear Slots->SlotEntityIds[SlotIdx] = 0 after.
	 *
	 * V2 PATCH 1: ExpectedPortIndex defends against accidental cross-port slot
	 * destruction. 0xFF = baseline-not-extension drain.
	 */
	FATUMGAME_API void DrainAndDestructSlot(
		flecs::entity StationE,
		::FCraftingSlots* Slots,
		int32 SlotIdx,
		uint8 ExpectedPortIndex);

	/**
	 * V2 PATCH 1: ensure a per-port extension slot exists at SlotIdx with
	 * the given Role / SlotProfile / PortIndex identity. Spawns a new container
	 * entity if missing or identity-broken; preserves existing entity if alive.
	 */
	FATUMGAME_API void EnsurePortSlot(
		flecs::entity StationE,
		::FCraftingSlots* Slots,
		int32 SlotIdx,
		ESlotRole Role,
		const UFlecsContainerProfile* SlotProfile,
		uint8 PortIndex);

	/**
	 * Phase 4 — recompute derived state (effective slot count, fuel ceiling,
	 * missing parts bitmask). Called after attach / detach / part death.
	 * V2 PATCH 1: port-indexed slot management; V2 PATCH 2: force-cancel before Disabled.
	 */
	FATUMGAME_API void RecomputeEffectiveLayout(flecs::entity StationE);

	/**
	 * Phase 4 — sim-thread attach part to station extension port. Validates
	 * station phase + port acceptance + stack cap; reserves part item via
	 * FPendingPartAttach + station via FPendingStationAttach (V2 PATCH 3 idempotency);
	 * marshals to game thread for SpawnEntity, then back to sim for FinalizePartAttach.
	 *
	 * @param PartItem    The candidate inventory item entity (will be consumed on success).
	 * @param StationE    Target station entity (must have FMultiblockExtensions).
	 * @param PortIndex   Index into Blueprint->ExtensionPorts.
	 * @param WeakWorld   UWorld captured by the BP entry on the GAME THREAD (NEVER call
	 *                    UFlecsArtillerySubsystem::GetWorld() from sim — UObjectArray race).
	 *                    Must be a live weak ptr at call time; AsyncTask will Get() on game thread.
	 * @return true if reservation accepted (game-thread spawn marshal pending);
	 *         false on rejection (port full, role mismatch, station busy, etc.).
	 */
	FATUMGAME_API bool AttachPartToStation(
		flecs::entity PartItem,
		flecs::entity StationE,
		int32 PortIndex,
		TWeakObjectPtr<UWorld> WeakWorld);

	/**
	 * Phase 4 — sim-thread continuation, called from EnqueueCommand after the
	 * game-thread SpawnEntity completes. Bonds the new child + clears reservations.
	 */
	FATUMGAME_API void FinalizePartAttach(
		FSkeletonKey StationKey,
		int32 PortIndex,
		FSkeletonKey NewChildKey,
		int64 OldItemId);

	/**
	 * Phase 4 — sim-thread detach a swappable child or extension. Restores the
	 * child to Dynamic + pickup tags, applies impulse pop, recomputes layout.
	 *
	 * @return false if child is rigid (not bSwappable) or station is mid-process.
	 */
	FATUMGAME_API bool DetachPartFromStation(flecs::entity ChildE);

	/**
	 * Phase 4 — full station deconstruct. Symmetric inverse of BondMultiblock +
	 * SetupStationInstance. Restores all children + the anchor to Dynamic + Pickup,
	 * destructs slot containers, strips station components.
	 *
	 * Gated on (a) station Idle/Disabled (b) ALL slots empty.
	 */
	FATUMGAME_API void DeconstructStation(flecs::entity StationE);

	// ═══════════════════════════════════════════════════════════════
	// PHASE 5a — TRANSPORT (ports + game-thread preview helper)
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Phase 5a — resolve port world position from FStationPorts.Ports[PortIndex].LocalOffsetCm.
	 * Reuses ReadStationWorldTransform (Barrage body lookup). Returns ZeroVector on lookup
	 * failure (no FStationPorts, invalid port index, or no Barrage body).
	 *
	 * Sim-thread safe (reads Barrage transforms via existing helper). Game-thread safe
	 * for preview when called inline (Barrage transform read is lock-free atomic).
	 *
	 * Cite: V2 PATCH 9.
	 */
	FATUMGAME_API FVector ResolvePortWorldPosition(flecs::entity Station, int32 PortIndex);

	/**
	 * Phase 5a — accessible Phase-4 detach helpers re-exposed for connector detach reuse.
	 * (Originally file-local in FlecsMultiblockRuntime.cpp.) RequestConnectorDetach mirrors
	 * the wrench-detach Phase 4 ordering: tags + grace BEFORE body wake to suppress
	 * same-tick PickupCollisionSystem auto-pickup.
	 */
	FATUMGAME_API void InstallPickupGrace(flecs::entity ChildE, int64 OwnerStationId, float GraceSeconds);
}
