// Niagara VFX profile for entity definitions.
// Defines attached effects (trails, glow) and death effects (explosions).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NiagaraSystem.h"
#include "FlecsNiagaraProfile.generated.h"

/**
 * Niagara VFX profile - defines visual effects for an entity.
 *
 * Two effect types:
 * - AttachedEffect: Continuous effect that follows the entity (trail, glow, sparks).
 *   Driven by Array Data Interface — one Niagara actor per effect type, zero per-entity UObjects.
 * - DeathEffect: Fire-and-forget burst spawned at entity's last position on death.
 *
 * Niagara Systems must expose User Parameters:
 *   AttachedEffect: "EntityPositions" (Vector Array), "EntityVelocities" (Vector Array)
 *   DeathEffect: Standard burst system, no special parameters needed.
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsNiagaraProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// ATTACHED EFFECT (continuous, follows entity via Array DI)
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Attached")
	TObjectPtr<UNiagaraSystem> AttachedEffect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Attached")
	float AttachedEffectScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Attached")
	FVector AttachedOffset = FVector::ZeroVector;

	// ═══════════════════════════════════════════════════════════════
	// DEATH EFFECT (fire-and-forget via SpawnSystemAtLocation)
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Death")
	TObjectPtr<UNiagaraSystem> DeathEffect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Death")
	float DeathEffectScale = 1.0f;

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	bool HasAttachedEffect() const { return AttachedEffect != nullptr; }
	bool HasDeathEffect() const { return DeathEffect != nullptr; }
	bool HasAnyEffect() const { return HasAttachedEffect() || HasDeathEffect(); }
};
