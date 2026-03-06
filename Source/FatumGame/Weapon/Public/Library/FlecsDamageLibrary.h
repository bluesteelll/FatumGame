// Blueprint function library for Flecs ECS damage and healing operations.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SkeletonTypes.h"
#include "GameplayTagContainer.h"
#include "FlecsDamageLibrary.generated.h"

class UFlecsArtillerySubsystem;

UCLASS()
class UFlecsDamageLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// DAMAGE & HEALING (game-thread safe, enqueued to simulation thread)
	// ═══════════════════════════════════════════════════════════════

	/** Mark an entity as dead by its Barrage SkeletonKey. Cleaned up next tick. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Entity", meta = (WorldContext = "WorldContextObject"))
	static void KillEntityByBarrageKey(UObject* WorldContextObject, FSkeletonKey BarrageKey);

	/** Apply damage to an entity by its Barrage SkeletonKey. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Damage", meta = (WorldContext = "WorldContextObject"))
	static void ApplyDamageByBarrageKey(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Damage);

	/**
	 * Apply damage with damage type and options.
	 * @param BarrageKey Target entity key.
	 * @param Damage Base damage amount.
	 * @param DamageType Damage type tag for resistances.
	 * @param bIgnoreArmor If true, bypasses armor calculation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Damage", meta = (WorldContext = "WorldContextObject"))
	static void ApplyDamageWithType(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Damage,
	                                FGameplayTag DamageType, bool bIgnoreArmor = false);

	/** Heal an entity by its Barrage SkeletonKey. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Damage", meta = (WorldContext = "WorldContextObject"))
	static void HealEntityByBarrageKey(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Amount);

	// ═══════════════════════════════════════════════════════════════
	// QUERY (game-thread) - CROSS-THREAD READ
	// Values may be stale by 1-2 frames. Safe for UI/cosmetics.
	// ═══════════════════════════════════════════════════════════════

	UFUNCTION(BlueprintPure, Category = "Flecs|Query", meta = (WorldContext = "WorldContextObject"))
	static float GetEntityHealth(UObject* WorldContextObject, FSkeletonKey BarrageKey);

	UFUNCTION(BlueprintPure, Category = "Flecs|Query", meta = (WorldContext = "WorldContextObject"))
	static float GetEntityMaxHealth(UObject* WorldContextObject, FSkeletonKey BarrageKey);

	UFUNCTION(BlueprintPure, Category = "Flecs|Query", meta = (WorldContext = "WorldContextObject"))
	static bool IsEntityAlive(UObject* WorldContextObject, FSkeletonKey BarrageKey);

	// ═══════════════════════════════════════════════════════════════
	// SIMULATION THREAD API (C++ only, for collision handlers)
	// ═══════════════════════════════════════════════════════════════

	static bool ApplyDamage_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey, float Damage);
	static void Heal_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey, float Amount);
	static bool IsAlive_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey);
	static bool GetHealth_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey,
		float& OutCurrentHP, float& OutMaxHP);
};
