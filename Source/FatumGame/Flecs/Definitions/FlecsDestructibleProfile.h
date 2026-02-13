// Destructible profile — makes an entity fragmentable on impact.
// Added as an optional profile on UFlecsEntityDefinition.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsDestructibleProfile.generated.h"

class UFlecsDestructibleGeometry;
class UFlecsEntityDefinition;

/**
 * Destructible profile — defines how an entity fragments when hit.
 *
 * Add to UFlecsEntityDefinition to make any object destructible:
 * walls, crates, pillars, chests, etc.
 *
 * The object's other profiles (Health, Interaction, Container, Niagara)
 * apply to the INTACT object. Fragment profiles come from
 * DefaultFragmentDefinition or per-fragment overrides.
 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class FATUMGAME_API UFlecsDestructibleProfile : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// FRAGMENT GEOMETRY
	// ═══════════════════════════════════════════════════════════════

	/** Fragment geometry collection — pre-baked layout from Blender fracture.
	 *  Reusable: same geometry can be shared across different object types. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fragments")
	TObjectPtr<UFlecsDestructibleGeometry> Geometry;

	/** Default Flecs profiles for ALL fragments (Niagara, Damage, Health, etc.)
	 *  Individual fragments can override via FDestructibleFragment::OverrideDefinition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fragments")
	TObjectPtr<UFlecsEntityDefinition> DefaultFragmentDefinition;

	// ═══════════════════════════════════════════════════════════════
	// PHYSICS
	// ═══════════════════════════════════════════════════════════════

	/** Force threshold for constraint breaking (Newtons). Higher = harder to break. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (ClampMin = "0.0"))
	float ConstraintBreakForce = 5000.0f;

	/** Torque threshold for constraint breaking. 0 = don't check torque. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (ClampMin = "0.0"))
	float ConstraintBreakTorque = 0.0f;

	/** Scale projectile impulse applied to nearest fragments on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (ClampMin = "0.0"))
	float ImpulseMultiplier = 1.0f;

	// ═══════════════════════════════════════════════════════════════
	// CLEANUP
	// ═══════════════════════════════════════════════════════════════

	/** If false, debris persists forever (until manually destroyed) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cleanup")
	bool bAutoDestroyDebris = true;

	/** Seconds before debris is auto-destroyed (only if bAutoDestroyDebris) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cleanup",
		meta = (EditCondition = "bAutoDestroyDebris", ClampMin = "0.1"))
	float DebrisLifetime = 10.0f;

	// ═══════════════════════════════════════════════════════════════
	// POOL
	// ═══════════════════════════════════════════════════════════════

	/** Pre-allocate this many Barrage bodies at BeginPlay (0 = grow on demand) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pool", meta = (ClampMin = "0"))
	int32 PrewarmPoolSize = 0;

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	bool IsValid() const { return Geometry != nullptr; }
	bool HasFragmentDefinition() const { return DefaultFragmentDefinition != nullptr; }
};
