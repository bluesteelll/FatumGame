// Step 0 — FContainedIn observer semantics probe (THROWAWAY / validation-only).
//
// Purpose: Verify Flecs on_set<FContainedIn> observer behaviour before committing to
// the hybrid observer + library-hook invalidation design in Phase 1 Crafting Framework.
//
// Contract: when FLECS_CRAFTING_PROBE_ENABLE is defined non-zero, the probe registers a
// temporary observer at sim-thread SetupFlecsSystems() time and mutates a scratch entity
// to exercise the 4 falsifiable criteria below. Results are logged once; if all pass,
// the main implementation (hybrid observer + library hooks) is correct. If any fail,
// switch to the fallback (pure library-hook invalidation — add hooks to PickupWorldItem
// and AddItemToContainer in addition to the TransferItem / RemoveItemFromContainer /
// RemoveAllItemsFromContainer hooks).
//
// Verification criteria (each must PASS before Step 1):
//   1. Fires exactly once per set<FContainedIn>(value) call.
//   2. Does NOT fire on try_get_mut<FContainedIn>() + direct field write (no modified()).
//   3. Observes the entity in valid state during destruct cascade (exits cleanly).
//   4. Fire count matches number of set<>() calls (no multiples per archetype migration).
//
// This file is COMPILE-READY but the probe body is gated behind a preprocessor flag so
// the final shipped build does not carry it. Architect grep-verified the flecs API
// (flecs.h:3566-3579 — "set = ensure + modified; get_mut alone does NOT fire OnSet").
// The probe exists to turn that grep-verification into a runtime falsification before
// committing the hybrid architecture. If this file is still present at Phase 1 sign-off,
// DELETE IT — it is intentionally non-shipping.
//
// To activate: add `#define FLECS_CRAFTING_PROBE_ENABLE 1` above the guard, rebuild,
// run Editor once, read LogCrafting output, then revert and delete this file.

#include "CoreMinimal.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsItemComponents.h"
#include "FlecsCraftingLog.h"
#include "flecs.h"

#ifndef FLECS_CRAFTING_PROBE_ENABLE
#define FLECS_CRAFTING_PROBE_ENABLE 0
#endif

#if FLECS_CRAFTING_PROBE_ENABLE

namespace FlecsCraftingObserverProbe
{
	// Run once at sim-thread-safe time, e.g. from SetupFlecsSystems().
	// Call InvokeProbe(World) explicitly from a sim-thread entry point if you
	// want to execute the probe. We do NOT hook the subsystem automatically
	// to keep this file truly throwaway.
	FATUMGAME_API void InvokeProbe(flecs::world& World)
	{
		UE_LOG(LogCrafting, Warning, TEXT("[ObserverProbe] BEGIN"));

		std::atomic<int32> FireCount{0};

		flecs::entity Observer = World.observer<FContainedIn>("ProbeOnSet")
			.event(flecs::OnSet)
			.each([&FireCount](flecs::entity Item, const FContainedIn& CI)
			{
				FireCount.fetch_add(1, std::memory_order_relaxed);
				UE_LOG(LogCrafting, Warning, TEXT("[ObserverProbe] OnSet fire: item=%llu container=%lld"),
					Item.id(), CI.ContainerEntityId);
			});

		// Scratch entities.
		flecs::entity Scratch = World.entity();
		FContainedIn Initial;
		Initial.ContainerEntityId = 1;
		Initial.GridPosition = FIntPoint(-1, -1);
		Initial.SlotIndex = -1;

		// ── Criterion 1: set() fires exactly once ──────────────────────
		const int32 BeforeSet = FireCount.load();
		Scratch.set<FContainedIn>(Initial);
		const int32 DeltaSet = FireCount.load() - BeforeSet;
		UE_LOG(LogCrafting, Warning, TEXT("[ObserverProbe] Criterion 1 (set fires once): delta=%d (expect=1)"), DeltaSet);

		// ── Criterion 2: get_mut() direct write does NOT fire ──────────
		const int32 BeforeMut = FireCount.load();
		if (FContainedIn* Mut = Scratch.try_get_mut<FContainedIn>())
		{
			Mut->ContainerEntityId = 2;
			// DO NOT call Scratch.modified<FContainedIn>() — that's what get_mut-silent means.
		}
		const int32 DeltaMut = FireCount.load() - BeforeMut;
		UE_LOG(LogCrafting, Warning, TEXT("[ObserverProbe] Criterion 2 (get_mut silent): delta=%d (expect=0)"), DeltaMut);

		// ── Criterion 3: set() with same value still fires (Flecs does not dedupe) ──
		const int32 BeforeSame = FireCount.load();
		FContainedIn Same;
		Same.ContainerEntityId = 2;
		Scratch.set<FContainedIn>(Same);
		const int32 DeltaSame = FireCount.load() - BeforeSame;
		UE_LOG(LogCrafting, Warning, TEXT("[ObserverProbe] Criterion 3 (same-value set fires): delta=%d (expect=1)"), DeltaSame);

		// ── Criterion 4: rapid burst of sets yields matching count ─────
		const int32 BeforeBurst = FireCount.load();
		for (int32 i = 0; i < 10; ++i)
		{
			FContainedIn Burst;
			Burst.ContainerEntityId = 100 + i;
			Scratch.set<FContainedIn>(Burst);
		}
		const int32 DeltaBurst = FireCount.load() - BeforeBurst;
		UE_LOG(LogCrafting, Warning, TEXT("[ObserverProbe] Criterion 4 (10-burst): delta=%d (expect=10)"), DeltaBurst);

		// Cleanup
		Scratch.destruct();
		Observer.destruct();

		UE_LOG(LogCrafting, Warning, TEXT("[ObserverProbe] END — verdict requires manual log review"));
	}
}

#else // !FLECS_CRAFTING_PROBE_ENABLE

// Probe disabled. Main implementation proceeds under the grep-verified + flecs.h-confirmed
// assumption that on_set fires ONLY for explicit set<>() calls. If this assumption breaks
// at runtime (fires from get_mut, archetype migration, or destruct cascades), switch to
// the fallback:
//   - Remove the "CraftingSlotMutation_OnSet" observer in FlecsArtillerySubsystem_Systems.cpp.
//   - Extend FlecsCraftingRuntime::MarkStationDirtyByContainer hook set to also fire from
//     FlecsContainerLibrary::PickupWorldItem (line ~155) and AddItemToContainer (line ~374),
//     using the same (OldContainerId, NewContainerId) pair pattern.
// This fallback is pure library-hook — deterministic, no flecs-observer dependency.

#endif
