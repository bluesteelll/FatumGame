// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Damage profile for Flecs entity spawning.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FlecsDamageProfile.generated.h"

/**
 * Data Asset defining damage properties for entity spawning.
 *
 * Used with FEntitySpawnRequest to make an entity deal damage on contact.
 * Entities without DamageProfile don't deal contact damage.
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsDamageProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// DAMAGE
	// ═══════════════════════════════════════════════════════════════

	/** Base damage dealt on contact */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0"))
	float Damage = 10.f;

	/** Damage type tag (for resistances, effects) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	FGameplayTag DamageType;

	/** Damage multiplier for critical hits */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "1"))
	float CriticalMultiplier = 2.f;

	/** Chance for critical hit (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0", ClampMax = "1"))
	float CriticalChance = 0.f;

	// ═══════════════════════════════════════════════════════════════
	// AREA DAMAGE
	// ═══════════════════════════════════════════════════════════════

	/** If true, deals area damage on impact */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area Damage")
	bool bAreaDamage = false;

	/** Radius of area damage (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area Damage", meta = (ClampMin = "0", EditCondition = "bAreaDamage"))
	float AreaRadius = 200.f;

	/** Damage falloff at edge of area (0 = full damage everywhere, 1 = no damage at edge) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area Damage", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bAreaDamage"))
	float AreaFalloff = 0.5f;

	// ═══════════════════════════════════════════════════════════════
	// BEHAVIOR
	// ═══════════════════════════════════════════════════════════════

	/** If true, entity is destroyed after dealing damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bDestroyOnHit = true;

	/** If true, can damage the same target multiple times */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bCanHitSameTargetMultipleTimes = false;

	/** Minimum time between hits on same target (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior", meta = (ClampMin = "0", EditCondition = "bCanHitSameTargetMultipleTimes"))
	float MultiHitCooldown = 0.5f;

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	float GetDamageAtDistance(float Distance) const
	{
		if (!bAreaDamage || AreaRadius <= 0.f) return Damage;
		float Ratio = FMath::Clamp(Distance / AreaRadius, 0.f, 1.f);
		return Damage * FMath::Lerp(1.f, 1.f - AreaFalloff, Ratio);
	}
};
