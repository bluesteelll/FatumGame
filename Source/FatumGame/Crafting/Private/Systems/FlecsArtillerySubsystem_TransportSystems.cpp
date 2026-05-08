// FlecsArtillerySubsystem_TransportSystems — Phase 5a transport graph systems.
//
// Hosts:
//   1. NetworkRebuildSystem               — drains atomic rebuild flag, runs full BFS
//   2. ClearStaleConnectorReservationsSystem — backstop sweep for FPendingConnectorPlace
//
// Phase placement: flecs::OnUpdate. Order:
//   NetworkRebuildSystem (FIRST in transport block — rebuilds before any consumer reads)
//   ClearStaleConnectorReservationsSystem (after rebuild — independent timeout sweep)
//
// 5a is topology only; consumers (PowerScheduler, SmelterCompleteEmit, etc.) are 5b/5c.

#include "FlecsArtillerySubsystem.h"

#include "Components/FlecsTransportComponents.h"
#include "FlecsCraftingLog.h"
#include "FlecsCraftingTransportSubsystem.h"

#include "flecs.h"

void UFlecsArtillerySubsystem::SetupTransportSystems()
{
	checkf(FlecsWorld, TEXT("SetupTransportSystems: FlecsWorld must be initialised"));
	flecs::world& World = *FlecsWorld;

	// ─────────────────────────────────────────────────────────
	// NetworkRebuildSystem
	// Runs FIRST in the transport block. Atomic check-and-clear; cheap on idle frames.
	// ─────────────────────────────────────────────────────────
	World.system<>("NetworkRebuildSystem")
		.kind(flecs::OnUpdate)
		.run([](flecs::iter& It)
		{
			UFlecsCraftingTransportSubsystem* Sub = UFlecsCraftingTransportSubsystem::SelfPtr;
			if (!Sub) return;
			if (!Sub->ConsumeRebuildRequest()) return;

			flecs::world W = It.world();
			Sub->RebuildAndPublish(W);
		});

	// ─────────────────────────────────────────────────────────
	// ClearStaleConnectorReservationsSystem
	// Backstop: 60-tick timeout for FPendingConnectorPlace if AsyncTask never completes.
	// Cite: V2 PATCH 1 verbatim Step 9.
	// ─────────────────────────────────────────────────────────
	World.system<FPendingConnectorPlace>("ClearStaleConnectorReservationsSystem")
		.kind(flecs::OnUpdate)
		.each([](flecs::iter& It, size_t Idx, FPendingConnectorPlace& P)
		{
			const uint64 Now = It.world().get_info()->frame_count_total;
			if (Now > P.EnqueuedTickStamp + kPendingConnectorTimeoutTicks)
			{
				flecs::entity E = It.entity(Idx);
				UE_LOG(LogCrafting, Warning,
					TEXT("[Transport] Stale connector reservation cleared on entity=%llu port=%u after %llu ticks"),
					(unsigned long long)E.id(),
					(uint32)P.ReservedPortIndex,
					(unsigned long long)kPendingConnectorTimeoutTicks);
				E.remove<FPendingConnectorPlace>();
			}
		});

	UE_LOG(LogCrafting, Log, TEXT("[Transport] SetupTransportSystems: 2 systems registered"));
}
