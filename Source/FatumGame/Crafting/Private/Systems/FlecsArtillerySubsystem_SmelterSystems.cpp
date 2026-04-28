// SmelterProcessSystem — Phase 3.
//
// Runs in the sim-thread crafting block, BEFORE CraftingSnapshotFlushSystem so that
// any bSnapshotDirty=true / Phase transition done here is observed by the same-tick
// flush. State machine bodies live in FlecsCraftingRuntime; this file only registers
// the system and wires the per-station tick into the runtime entry points.
//
// See `.claude/CRAFTING_PHASE3_BLUEPRINT.md` §1.5.1 for the algorithm spec.

#include "FlecsArtillerySubsystem.h"

#include "FlecsBarrageComponents.h"
#include "FlecsCraftingLog.h"
#include "FlecsCraftingTypes.h"
#include "FlecsCraftingUISubsystem.h"
#include "Components/FlecsCraftingComponents.h"
#include "Library/FlecsCraftingRuntime.h"

#include "flecs.h"

void UFlecsArtillerySubsystem::SetupSmelterSystems()
{
	checkf(FlecsWorld, TEXT("SetupSmelterSystems: FlecsWorld must be initialised"));
	flecs::world& World = *FlecsWorld;

	// ─────────────────────────────────────────────────────────
	// SmelterProcessSystem
	// Query: Smelter stations only (FSmelterInstance is the marker — only Smelter
	// SetupStationInstance branch attaches it). Skip teardown stations.
	// ─────────────────────────────────────────────────────────
	World.system<
		const FCraftingStationStatic,
		FCraftingStationInstance,
		FSmelterInstance,
		const FCraftingSlots>("SmelterProcessSystem")
		.with<FTagCraftingStation>()
		.without<FTagCraftingStationDestroying>()
		.each([](
			flecs::entity Station,
			const FCraftingStationStatic& Static,
			FCraftingStationInstance& Inst,
			FSmelterInstance& SmInst,
			const FCraftingSlots& Slots)
		{
			const flecs::world W = Station.world();
			const float DT = W.delta_time();
			if (DT <= 0.f) return;  // paused world / first-tick edge

			switch (SmInst.Phase)
			{
			case EProcessPhase::Idle:
				if (SmInst.bStartRequested)
				{
					SmInst.bStartRequested = false;
					FlecsCraftingRuntime::TrySmelterStart(Station, Static, Inst, SmInst, Slots);
				}
				break;

			case EProcessPhase::Processing:
				if (SmInst.bCancelRequested)
				{
					SmInst.bCancelRequested = false;
					FlecsCraftingRuntime::SmelterCancel(Station, Static, Inst, SmInst, Slots);
					break;
				}
				FlecsCraftingRuntime::SmelterTickProcessing(Station, Static, Inst, SmInst, DT);
				break;

			case EProcessPhase::Stalled:
				if (SmInst.bCancelRequested)
				{
					SmInst.bCancelRequested = false;
					FlecsCraftingRuntime::SmelterCancel(Station, Static, Inst, SmInst, Slots);
					break;
				}
				FlecsCraftingRuntime::SmelterTickStalled(Station, Inst, SmInst);
				break;

			case EProcessPhase::Completing:
				FlecsCraftingRuntime::SmelterComplete(Station, Static, Inst, SmInst, Slots);
				break;

			case EProcessPhase::Cancelled:
				// Refund already executed in SmelterCancel — clear lock, return to Idle.
				FlecsCraftingRuntime::SmelterFinalizeCancel(Station, Inst, SmInst, Slots);
				break;

			case EProcessPhase::Disabled:
				// Phase 4 — terminal-no-op while a required functional is missing.
				// Drain any racing requests.
				if (SmInst.bStartRequested)
				{
					SmInst.bStartRequested = false;
					UE_LOG(LogCrafting, Verbose,
						TEXT("[Smelter] '%s' START rejected — station Disabled (missing required functional)"),
						*Static.StationName.ToString());
				}
				if (SmInst.bCancelRequested)
				{
					SmInst.bCancelRequested = false;
					// No-op — Disabled has no in-flight ledger to refund.
				}
				break;
			}

			// Publish phase atomic for cosmetic UI consumers, every tick. See
			// FCraftingStationSharedState::ProcessPhasePacked for the contract (release
			// store here / acquire load on game-thread widget Tick).
			if (UFlecsCraftingUISubsystem* Sub = UFlecsCraftingUISubsystem::SelfPtr)
			{
				const FBarrageBody* Body = Station.try_get<FBarrageBody>();
				if (Body && Body->IsValid())
				{
					if (FCraftingStationSharedState* Shared = Sub->FindSharedState(Body->BarrageKey))
					{
						Shared->ProcessPhasePacked.store(
							static_cast<uint8>(SmInst.Phase),
							std::memory_order_release);
					}
				}
			}

			// Drain redundant flags (paranoia — Idle branch already drained, but multi-tick
			// transient phases may have stray flag flips queued from BP API).
			SmInst.bStartRequested  = false;
			SmInst.bCancelRequested = false;

			// Verbose per-tick log when actively processing — gate-keep spam.
			if (SmInst.Phase == EProcessPhase::Processing)
			{
				UE_LOG(LogCrafting, Verbose,
					TEXT("[Smelter] '%s' tick Progress=%.2f/%.2f Reservoir=%.2fs DT=%.4f"),
					*Static.StationName.ToString(),
					SmInst.ProgressSeconds, SmInst.DurationCached,
					Inst.FuelChargeSecondsRemaining, DT);
			}
		});

	UE_LOG(LogCrafting, Log, TEXT("[SetupSmelterSystems] SmelterProcessSystem registered"));
}
