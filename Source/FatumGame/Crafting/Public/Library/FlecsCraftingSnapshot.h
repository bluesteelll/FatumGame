// Game-thread readable crafting station snapshot.
// Built by sim-thread FlecsCraftingRuntime::BuildSnapshot, published via TTripleBuffer
// in UFlecsCraftingUISubsystem::StationSharedStates.

#pragma once

#include "CoreMinimal.h"
#include "FlecsCraftingSnapshot.generated.h"

// ═══════════════════════════════════════════════════════════════
// SLOT SNAPSHOT (per-slot row)
// ═══════════════════════════════════════════════════════════════

/**
 * One slot's contents summary. ItemLines holds a brief textual representation
 * ("5× Copper Ingot") — display-only, rebuilt each publish.
 */
USTRUCT(BlueprintType)
struct FATUMGAME_API FCraftingStationSlotSnapshot
{
	GENERATED_BODY()

	/** Slot label copied from UFlecsCraftingStationProfile::SlotLayout[i].SlotName. */
	UPROPERTY(BlueprintReadOnly, Category = "Slot")
	FName SlotName;

	/** ESlotRole narrowed to uint8 for BP exposure. */
	UPROPERTY(BlueprintReadOnly, Category = "Slot")
	uint8 Role = 0;

	/** Number of distinct items in this slot. */
	UPROPERTY(BlueprintReadOnly, Category = "Slot")
	int32 ItemCount = 0;

	/** Pre-formatted display lines (not a UPROPERTY to keep inline allocator). */
	TArray<FText, TInlineAllocator<8>> ItemLines;
};

// ═══════════════════════════════════════════════════════════════
// STATION SNAPSHOT
// ═══════════════════════════════════════════════════════════════

/**
 * Full station state snapshot — single row published per-station per-dirty-tick.
 * POD-ish (has FText / TArray); safe to MoveTemp into TTripleBuffer.
 */
USTRUCT(BlueprintType)
struct FATUMGAME_API FCraftingStationSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	FName StationName;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	FText DebugDisplayName;

	/** Recipe display name for the matched recipe, or empty when no match. */
	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	FName MatchedRecipeName;

	/** ECraftingMatchDiagnostic narrowed to uint8. */
	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	uint8 Diagnostic = 0;

	/** EFuelType narrowed to uint8. */
	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	uint8 ActiveFuelType = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	float FuelChargeSecondsRemaining = 0.f;

	/** EProcessPhase narrowed to uint8 — Smelter (or future Press / Forge) state. 0 = Idle. */
	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	uint8 ProcessPhase = 0;

	/** Wall-clock seconds elapsed in the current process. 0 when not processing. */
	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	float ProgressSeconds = 0.f;

	/** Cached duration (copied from MatchedRecipe->DurationSeconds at Start). 0 when not processing. */
	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	float DurationSecondsCached = 0.f;

	/** Phase 4 — bitmask of missing required functional roles (UI hint colour: red = missing). */
	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	int32 MissingPartsBitmask = 0;

	/** Phase 4 — count of currently-occupied extension ports on this station. */
	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	uint8 ExtensionPortsOccupied = 0;

	/** One row per configured slot (inline allocator — snapshot holds up to 8 inline). */
	TArray<FCraftingStationSlotSnapshot, TInlineAllocator<8>> Slots;

	/** Sim-thread publish counter for debugging. Not UPROPERTY — uint32 not BP-supported. */
	uint32 FrameStamp = 0;
};
