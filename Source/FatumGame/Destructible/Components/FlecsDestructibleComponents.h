// Destructible object components for Flecs entities.

#pragma once

#include "CoreMinimal.h"

class UFlecsDestructibleProfile;

// ═══════════════════════════════════════════════════════════════
// DESTRUCTIBLE STATIC
// ═══════════════════════════════════════════════════════════════

/**
 * Static destructible data — lives on entity (not prefab, since it holds a UObject*).
 * Presence of this component identifies an entity as fragmentable.
 */
struct FDestructibleStatic
{
	/** Reference to the destructible profile (fragment geometry, break force, etc.) */
	UFlecsDestructibleProfile* Profile = nullptr;

	bool IsValid() const { return Profile != nullptr; }
};

// ═══════════════════════════════════════════════════════════════
// DEBRIS INSTANCE
// ═══════════════════════════════════════════════════════════════

/**
 * Instance debris data - mutable per-fragment data.
 * Present on each fragment entity spawned from a destructible object.
 */
struct FDebrisInstance
{
	/** Remaining lifetime in seconds (only used if bAutoDestroy) */
	float LifetimeRemaining = 10.f;

	/** Should this fragment auto-destroy after lifetime expires? */
	bool bAutoDestroy = true;

	/** Index into FDebrisPool for body reuse. INDEX_NONE = not pooled. */
	int32 PoolSlotIndex = INDEX_NONE;

	/** Mass to restore when all constraints break (kg). 0 = no mass change. */
	float FreeMassKg = 0.f;

	/** Deferred impulse (kg·cm/s, UE coords). Applied when fragment is freed. */
	FVector PendingImpulse = FVector::ZeroVector;

	/** True if this fragment belongs to a world-anchored structure. */
	bool bInAnchoredStructure = false;
};

// ═══════════════════════════════════════════════════════════════
// FRAGMENTATION DATA
// ═══════════════════════════════════════════════════════════════

/**
 * Fragmentation event data — stored on collision pair entity.
 * Contains impact info needed by FragmentationSystem to apply impulse.
 */
struct FFragmentationData
{
	/** World-space impact point */
	FVector ImpactPoint = FVector::ZeroVector;

	/** Impact direction (normalized) */
	FVector ImpactDirection = FVector::ZeroVector;

	/** Impact impulse magnitude */
	float ImpactImpulse = 0.f;
};
