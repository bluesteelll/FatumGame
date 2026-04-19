// Multiblock assembly — Phase 2 of the Crafting System.
//
// See CRAFTING_PHASE2_BLUEPRINT_V2.md + V3.md for design.
// Two-pass detection (collect candidates, bond after iterator closes) prevents
// archetype migration mid-query. Scan runs at 2 Hz (every 30 sim ticks).

#include "FlecsArtillerySubsystem.h"
#include "FlecsCraftingLog.h"
#include "Components/FlecsMultiblockComponents.h"

// Stub — filled in at Step 5 (detection) and Step 6 (bond).

void UFlecsArtillerySubsystem::SetupMultiblockSystems()
{
	// TODO(Step 5): detection system.
}

void UFlecsArtillerySubsystem::BondMultiblock(flecs::entity /*Anchor*/,
	const UFlecsMultiblockBlueprint* /*Blueprint*/,
	const TArray<int64, TInlineAllocator<15>>& /*ChildEntityIds*/)
{
	// TODO(Step 6): bond transaction.
}
