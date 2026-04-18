// CraftingSnapshotFlushSystem — Phase 1 Step 11.
//
// Runs LAST in sim-tick order (after DeadEntityCleanupSystem, before CollisionPairCleanupSystem).
// Iterates stations flagged bSnapshotDirty=true, runs MatchRecipe + BuildSnapshot, publishes
// to UFlecsCraftingUISubsystem's TTripleBuffer shared state, clears dirty flag.
//
// See blueprint v3 §1.5/§1.6 and critique v3 CR2 resolution (coalesce PushSnapshot per tick).

#include "FlecsArtillerySubsystem.h"
#include "FlecsCraftingLog.h"
#include "FlecsCraftingTypes.h"
#include "FlecsCraftingUISubsystem.h"
#include "Components/FlecsCraftingComponents.h"
#include "Library/FlecsCraftingRuntime.h"
#include "Library/FlecsCraftingSnapshot.h"
#include "FlecsCraftingRecipeRegistry.h"
#include "flecs.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

void UFlecsArtillerySubsystem::SetupCraftingSystems()
{
	flecs::world& World = *FlecsWorld;

	// Cache the recipe registry once — GameInstanceSubsystem, lives for the game session.
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UFlecsCraftingRecipeRegistry* Registry = GameInstance
		? GameInstance->GetSubsystem<UFlecsCraftingRecipeRegistry>()
		: nullptr;

	if (!Registry)
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[CraftingSnapshotFlushSystem] Recipe registry unavailable at setup — flush will skip matching."));
	}

	// ─────────────────────────────────────────────────────────
	// CraftingSnapshotFlushSystem
	// ─────────────────────────────────────────────────────────
	// Query: stations that are dirty AND not tearing down.
	// Mirrors the bEquipmentDirty flush pattern in VitalsSystems.
	World.system<FCraftingStationInstance, const FCraftingStationStatic, const FCraftingSlots>("CraftingSnapshotFlushSystem")
		.without<FTagCraftingStationDestroying>()
		.each([Registry](flecs::entity StationE,
		                 FCraftingStationInstance& Inst,
		                 const FCraftingStationStatic& /*Stat*/,
		                 const FCraftingSlots& /*Slots*/)
		{
			if (!Inst.bSnapshotDirty)
			{
				return;
			}

			// Compute digest — cheap. Skip match if unchanged (common during rapid flag flips).
			const uint64 NewDigest = FlecsCraftingRuntime::ComputeSlotDigest(StationE);

			if (NewDigest != Inst.LastMatchedDigest)
			{
				// Ingredient/fuel set changed → re-match.
				FlecsCraftingRuntime::MatchRecipe(StationE, Registry);
				Inst.LastMatchedDigest = NewDigest;
			}

			// Always rebuild snapshot when dirty (inventory mutations even without match change
			// need widget update — item counts, fuel charge, etc.).
			FCraftingStationSnapshot Snapshot;
			FlecsCraftingRuntime::BuildSnapshot(StationE, Snapshot);

			// Publish to subsystem-owned shared state.
			if (UFlecsCraftingUISubsystem* UISub = UFlecsCraftingUISubsystem::SelfPtr)
			{
				const FSkeletonKey StationKey(StationE.id());
				if (FCraftingStationSharedState* Shared = UISub->FindSharedState(StationKey))
				{
					// Triple-buffer publish: write current buffer, swap-publish.
					Shared->SnapshotBuffer.Write(MoveTemp(Snapshot));
					Shared->SnapshotBuffer.SwapWriteBuffers();
					Shared->SimVersion.fetch_add(1, std::memory_order_release);
				}
				// else: subsystem hasn't registered this station yet (first-tick ordering).
				// bSnapshotDirty stays true → next tick retries. Self-healing.
				else
				{
					return; // keep dirty flag so we republish next tick
				}
			}

			// Clear dirty flag only after successful publish.
			Inst.bSnapshotDirty = false;
		});
}
