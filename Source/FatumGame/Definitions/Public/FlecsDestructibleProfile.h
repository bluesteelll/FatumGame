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

	/** Effective projectile mass for impulse transfer (kg).
	 *  Controls how much momentum the projectile transfers to fragments on first hit.
	 *  ~0.1 = pistol, ~0.3 = rifle, ~1.0 = heavy ordnance.
	 *  Internally: impulse = projectile_speed * this value, applied via Jolt AddImpulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (ClampMin = "0.0"))
	float ImpulseMultiplier = 0.15f;

	/** Minimum impulse magnitude (cm/s) to trigger fragmentation from non-contact forces
	 *  (abilities, explosions). 0 = any impulse fragments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (ClampMin = "0.0"))
	float FragmentationForceThreshold = 2000.f;

	/** Mass while fragments are held by constraints (kg).
	 *  High value = minimal jitter from projectile impacts while structure is intact.
	 *  When last constraint breaks, mass is restored to FragmentMassKg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (ClampMin = "0.01"))
	float ConstrainedMassKg = 50.0f;

	/** Mass after fragment is freed from all constraints (kg).
	 *  Lower = lighter debris, more scatter on break. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (ClampMin = "0.01"))
	float FragmentMassKg = 1.0f;

	/** Anchor bottom fragments to the world via breakable constraints.
	 *  All bottom-layer fragments (lowest Z) get a Fixed constraint to Body::sFixedToWorld.
	 *  They remain Dynamic but held in place until the constraint breaks.
	 *  Use for objects attached to surfaces: statues, wall-mounted fixtures, etc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	bool bAnchorToWorld = false;

	/** Force threshold to detach a fragment from the world (Newtons).
	 *  Separate from ConstraintBreakForce so you can tune "rip off wall" vs "break apart".
	 *  Only used when bAnchorToWorld is true. 0 = unbreakable anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics",
		meta = (EditCondition = "bAnchorToWorld", ClampMin = "0.0"))
	float AnchorBreakForce = 10000.0f;

	/** Torque threshold to detach from world. 0 = don't check torque. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics",
		meta = (EditCondition = "bAnchorToWorld", ClampMin = "0.0"))
	float AnchorBreakTorque = 0.0f;

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
