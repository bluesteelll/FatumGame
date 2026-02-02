// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Health profile for Flecs entity spawning.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsHealthProfile.generated.h"

/**
 * Data Asset defining health properties for entity spawning.
 *
 * Used with FEntitySpawnRequest to add health/damage system to an entity.
 * Entities without HealthProfile can't take damage.
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsHealthProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// HEALTH
	// ═══════════════════════════════════════════════════════════════

	/** Maximum health points */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "1"))
	float MaxHealth = 100.f;

	/** Starting health (0 = start at MaxHealth) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0"))
	float StartingHealth = 0.f;

	/** Damage reduction (subtracted from incoming damage) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health", meta = (ClampMin = "0"))
	float Armor = 0.f;

	// ═══════════════════════════════════════════════════════════════
	// REGENERATION
	// ═══════════════════════════════════════════════════════════════

	/** Health regeneration per second (0 = no regen) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Regeneration", meta = (ClampMin = "0"))
	float RegenPerSecond = 0.f;

	/** Delay before regen starts after taking damage (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Regeneration", meta = (ClampMin = "0"))
	float RegenDelay = 0.f;

	// ═══════════════════════════════════════════════════════════════
	// BEHAVIOR
	// ═══════════════════════════════════════════════════════════════

	/** Invulnerability time after taking damage (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior", meta = (ClampMin = "0"))
	float InvulnerabilityTime = 0.f;

	/** If true, entity is destroyed when health reaches 0 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bDestroyOnDeath = true;

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	float GetStartingHealth() const { return StartingHealth > 0.f ? StartingHealth : MaxHealth; }
	bool HasRegen() const { return RegenPerSecond > 0.f; }
	bool HasArmor() const { return Armor > 0.f; }
};
