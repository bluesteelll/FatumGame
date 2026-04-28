// Blueprint function library for Flecs ECS crafting operations.
//
// Ownership contract:
//  - All read state goes via the snapshot channel owned by UFlecsCraftingUISubsystem
//    (forthcoming — Phase 1 Step 9). Never dereference live ECS components on game thread.
//  - All writes flow through UFlecsArtillerySubsystem::EnqueueCommand — no game-thread
//    mutation of crafting components.
//  - Sim-thread fields (MatchedRecipe, SlotDirtyDigest, LastMatchedDigest,
//    FuelChargeSecondsRemaining, ActiveFuelType) are sim-thread-write-only.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SkeletonTypes.h"
#include "FlecsCraftingTypes.h"
#include "FlecsCraftingLibrary.generated.h"

UCLASS()
class FATUMGAME_API UFlecsCraftingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// SIM-THREAD COMMAND API (game-thread entry points; enqueue to sim thread)
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Enqueues a sim-thread InvalidateMatchCache on the target station — flush system
	 * will rebuild snapshot next tick. Used by hover-widget on target change to force
	 * a fresh read even if the station's contents haven't changed since last publish.
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Crafting", meta = (WorldContext = "WorldContextObject"))
	static void RequestMatch(UObject* WorldContextObject, FSkeletonKey StationKey);

	/**
	 * Enqueues full station teardown. Sequence (sim thread):
	 *   1. Add FTagCraftingStationDestroying (stops observer + flush).
	 *   2. Enumerate slot entities → RemoveAllItemsFromContainer on each → destruct slot entity.
	 *   3. Destruct station entity.
	 *   4. AsyncTask(GameThread) to release the snapshot shared state (Step 9 integration TODO).
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Crafting", meta = (WorldContext = "WorldContextObject"))
	static void RequestStationDestroy(UObject* WorldContextObject, FSkeletonKey StationKey);

	/**
	 * Phase 3 — request a Smelter station to begin processing its currently-matched recipe.
	 * Idempotent: setting bStartRequested twice in one tick has no extra effect.
	 *
	 * Marshals to sim thread via EnqueueCommand. The actual gating (recipe match, ingredient
	 * count, fuel projection) happens in SmelterProcessSystem — this entry point only flips
	 * the FSmelterInstance::bStartRequested flag for the next tick to observe.
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Crafting", meta = (WorldContext = "WorldContextObject"))
	static void RequestSmelterStart(UObject* WorldContextObject, FSkeletonKey StationKey);

	/**
	 * Phase 3 — request a Smelter station to cancel any in-flight process.
	 * Cancel from Idle is a silent no-op. Cancel from Processing/Stalled refunds the
	 * ledger items to MaterialInput slots (or floor on overflow). Reservoir is NOT refunded.
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Crafting", meta = (WorldContext = "WorldContextObject"))
	static void RequestSmelterCancel(UObject* WorldContextObject, FSkeletonKey StationKey);

	// ═══════════════════════════════════════════════════════════════
	// PHASE 4 — MODULAR STATIONS (wrench attach/detach/deconstruct)
	// ═══════════════════════════════════════════════════════════════

	/** Phase 4 — request hot-swap detach of a swappable functional or extension.
	 *  ChildKey is the FSkeletonKey wrapping the child Flecs entity id (NOT BarrageKey). */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Crafting", meta = (WorldContext = "WorldContextObject"))
	static void RequestPartDetach(UObject* WorldContextObject, FSkeletonKey ChildKey);

	/** Phase 4 — request attach of a SPECIFIC inventory item to a SPECIFIC extension port.
	 *  StationKey + PartItemEntityId are Flecs entity ids (wrapped). */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Crafting", meta = (WorldContext = "WorldContextObject"))
	static void RequestPartAttach(
		UObject* WorldContextObject,
		FSkeletonKey StationKey,
		int32 PortIndex,
		int64 PartItemEntityId);

	/** Phase 4 — auto-pick the first compatible inventory item for a port. Useful when
	 *  the player presses E on a port without explicitly selecting an item. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Crafting", meta = (WorldContext = "WorldContextObject"))
	static void RequestPartAttachAuto(
		UObject* WorldContextObject,
		FSkeletonKey StationKey,
		int32 PortIndex,
		int64 PlayerInventoryEntityId);

	/** Phase 4 — request full station deconstruct (Hold-E + wrench on anchor). */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Crafting", meta = (WorldContext = "WorldContextObject"))
	static void RequestStationDeconstruct(UObject* WorldContextObject, FSkeletonKey StationKey);

	/** Phase 4 — pure query: is the given child swappable? Used by hover UI feedback. */
	UFUNCTION(BlueprintPure, Category = "Flecs|Crafting", meta = (WorldContext = "WorldContextObject"))
	static bool IsPartSwappable(UObject* WorldContextObject, FSkeletonKey ChildKey);

	// ═══════════════════════════════════════════════════════════════
	// UI HELPERS (static — no world dependency)
	// ═══════════════════════════════════════════════════════════════

	/** Human-readable diagnostic label for the UI. */
	UFUNCTION(BlueprintPure, Category = "Flecs|Crafting")
	static FText GetDiagnosticText(ECraftingMatchDiagnostic Diagnostic);

	/** Human-readable slot-role label for the UI. */
	UFUNCTION(BlueprintPure, Category = "Flecs|Crafting")
	static FText GetSlotRoleText(ESlotRole Role);
};
