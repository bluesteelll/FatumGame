// Explosion profile for Flecs entity spawning.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FlecsExplosionProfile.generated.h"

class UNiagaraSystem;

/**
 * Data Asset defining explosion properties for entity spawning.
 *
 * Used with FEntitySpawnRequest to make an entity explode on detonation.
 * Entities without ExplosionProfile don't produce blast waves.
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsExplosionProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// BLAST
	// ═══════════════════════════════════════════════════════════════

	/** Blast radius (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blast", meta = (ClampMin = "0"))
	float Radius = 500.f;

	/** Base damage at epicenter */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blast", meta = (ClampMin = "0"))
	float BaseDamage = 50.f;

	/** Radial impulse strength (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blast", meta = (ClampMin = "0"))
	float ImpulseStrength = 2000.f;

	/** Upward bias on impulse direction (0=pure radial, 0.5=moderate lift) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blast", meta = (ClampMin = "0", ClampMax = "1"))
	float VerticalBias = 0.3f;

	/** Offset epicenter along contact normal (cm) to push blast away from surface */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blast", meta = (ClampMin = "0"))
	float EpicenterLift = 5.f;

	// ═══════════════════════════════════════════════════════════════
	// FALLOFF
	// ═══════════════════════════════════════════════════════════════

	/** Damage falloff exponent: 1.0=linear, 2.0=quadratic, 0.0=flat (full damage everywhere) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Falloff", meta = (ClampMin = "0"))
	float DamageFalloff = 1.f;

	/** Impulse falloff exponent */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Falloff", meta = (ClampMin = "0"))
	float ImpulseFalloff = 1.f;

	// ═══════════════════════════════════════════════════════════════
	// BEHAVIOR
	// ═══════════════════════════════════════════════════════════════

	/** Can the explosion hurt the entity's owner (self-damage)? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bDamageOwner = false;

	/** Damage type tag for resistances */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	FGameplayTag DamageType;

	// ═══════════════════════════════════════════════════════════════
	// VFX
	// ═══════════════════════════════════════════════════════════════

	/** Niagara effect spawned at detonation point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<UNiagaraSystem> ExplosionEffect;

	/** Scale for explosion VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX", meta = (ClampMin = "0.01"))
	float ExplosionEffectScale = 1.f;
};
