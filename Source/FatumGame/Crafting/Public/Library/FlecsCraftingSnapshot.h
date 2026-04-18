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

	/** One row per configured slot (inline allocator — snapshot holds up to 8 inline). */
	TArray<FCraftingStationSlotSnapshot, TInlineAllocator<8>> Slots;

	/** Sim-thread publish counter for debugging. Not UPROPERTY — uint32 not BP-supported. */
	uint32 FrameStamp = 0;
};
