// Sim-thread-only multiblock runtime helpers (Phase 2 of the Crafting System).
//
// Namespace, not a UObject. All functions assume sim-thread reentry — safe to
// call from EnqueueCommand lambdas, Flecs systems, or spawner code after the
// entity has been bound to Barrage. NEVER call from game thread directly.

#pragma once

#include "CoreMinimal.h"

namespace flecs { struct entity; }
class UFlecsCraftingStationProfile;

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
	 * Phase 5 module-add uses a separate entry point (AddModuleToStation —
	 * NOT this function) so slot-index accounting stays sane.
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
}
