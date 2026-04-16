// Blueprint function library for Flecs ECS melee weapon control.
// Game-thread-safe wrappers that enqueue sim-thread state mutations on
// FMeleeWeaponInstance (bAttackRequested / bBlockRequested).
//
// Mirrors UFlecsWeaponLibrary (ranged) — same EnqueueCommand + try_get_mut pattern.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FlecsMeleeLibrary.generated.h"

UCLASS()
class UFlecsMeleeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// MELEE CONTROL (game-thread safe, enqueued to simulation thread)
	// ═══════════════════════════════════════════════════════════════

	/** Set FMeleeWeaponInstance.bAttackRequested on the weapon entity. Edge-trigger detection
	 *  lives in MeleeChargeSystem via bWasAttackRequestedLastTick. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Melee", meta = (WorldContext = "WorldContextObject"))
	static void SetMeleeAttackRequested(UObject* WorldContextObject, int64 WeaponEntityId, bool bRequested);

	/** Set FMeleeWeaponInstance.bBlockRequested on the weapon entity.
	 *  Phase 6 BlockAbsorbSystem will act on transitions; Phase 4 just stores the bit. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Melee", meta = (WorldContext = "WorldContextObject"))
	static void SetMeleeBlockRequested(UObject* WorldContextObject, int64 WeaponEntityId, bool bRequested);
};
