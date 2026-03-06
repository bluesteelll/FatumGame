// Projectile components for Flecs Prefabs and Entities.

#pragma once

#include "CoreMinimal.h"
#include "FlecsProjectileComponents.generated.h"

class UFlecsProjectileProfile;

// ═══════════════════════════════════════════════════════════════
// PROJECTILE STATIC
// ═══════════════════════════════════════════════════════════════

/**
 * Static projectile data - lives in PREFAB, shared by all projectiles of this type.
 * Contains immutable projectile rules.
 *
 * Instance data (LifetimeRemaining, CurrentBounces) is in FProjectileInstance.
 */
struct FProjectileStatic
{
	/** Maximum lifetime in seconds */
	float MaxLifetime = 10.f;

	/** Max bounces before destruction (-1 = infinite) */
	int32 MaxBounces = -1;

	/** Grace period frames after spawn/bounce before velocity check */
	int32 GracePeriodFrames = 30; // ~0.25 sec at 120Hz

	/** Minimum velocity before projectile is killed (units/sec) */
	float MinVelocity = 50.f;

	/** Should maintain constant speed? */
	bool bMaintainSpeed = false;

	/** Target speed if bMaintainSpeed (units/sec) */
	float TargetSpeed = 0.f;

	static FProjectileStatic FromProfile(const UFlecsProjectileProfile* Profile);
};

// ═══════════════════════════════════════════════════════════════
// PROJECTILE INSTANCE
// ═══════════════════════════════════════════════════════════════

/**
 * Instance projectile data - mutable per-entity data.
 * Static data (MaxBounces, MaxLifetime) comes from FProjectileStatic in prefab.
 */
USTRUCT(BlueprintType)
struct FProjectileInstance
{
	GENERATED_BODY()

	/** Remaining lifetime in seconds */
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	float LifetimeRemaining = 10.f;

	/** Current bounce count */
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	int32 BounceCount = 0;

	/** Grace frames remaining before velocity check */
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	int32 GraceFramesRemaining = 30;

	/** Entity that spawned this projectile (for friendly fire, damage attribution) */
	UPROPERTY(BlueprintReadWrite, Category = "Projectile")
	int64 OwnerEntityId = 0;

	/** Returns true if this projectile was fired by the given entity (self-damage prevention). */
	bool IsOwnedBy(uint64 EntityId) const
	{
		return OwnerEntityId != 0 && static_cast<uint64>(OwnerEntityId) == EntityId;
	}
};
