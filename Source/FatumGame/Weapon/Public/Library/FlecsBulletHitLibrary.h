// FlecsBulletHitLibrary — shared bullet-hit resolution for projectile and hitscan paths.
// Implements damage/crit/penetration/surface-degradation/fragmentation in a single
// utility consumed by PenetrationSystem (projectile path) and FlecsHitscanLibrary
// (hitscan path).

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

#include "flecs.h"

class UBarrageDispatch;
struct FPenetrationStatic;

/**
 * Input context for UFlecsBulletHitLibrary::ApplyBulletHit.
 * Not a USTRUCT — internal POD passed by const-ref.
 */
struct FBulletHitContext
{
	// ── Source / target identity ──
	uint64 ShooterEntityId = 0;      // Entity that fired (for IsOwnedBy self-damage check). 0 = unknown.
	uint64 TargetEntityId = 0;       // Target entity (must equal Target.raw_id()).

	// ── Geometry ──
	FVector ImpactPoint    = FVector::ZeroVector;
	FVector ImpactNormal   = FVector::ZeroVector;  // World-space surface normal at impact
	FVector IncomingDir    = FVector::ZeroVector;  // Unit vector, direction bullet is travelling
	FVector SpawnPosition  = FVector::ZeroVector;  // For distance-falloff (IsZero = disabled)

	// ── Bullet state ──
	float IncomingSpeed    = 0.f;                  // cm/s (used for fragmentation impulse)
	float BaseDamage       = 0.f;
	float StructuralDamage = 0.f;                  // >0 and target has FDestructibleStatic → used instead of BaseDamage
	float CritChance       = 0.f;
	float CritMultiplier   = 2.f;
	float DamageFalloffStart = 0.f;
	float InvFalloffRange  = 0.f;                  // 1/(End-Start). 0 = disabled.
	float MinDamageMultiplier = 0.f;               // Floor for distance-based attenuation

	// ── Impulse (hitscan only) ──
	float ImpulseStrength  = 0.f;                  // Scales impulse applied to dynamic target
	bool  bApplyImpulse    = false;

	// ── Surface degradation ──
	bool  bCanDegrade      = true;                 // Allow degrade + fragmentation triggering

	FGameplayTag DamageType;

	// ── Penetration ──
	const FPenetrationStatic* PenStatic = nullptr; // nullptr → non-penetrating path
	uint32 SubShapeIDValue = 0;                    // 0 = no compound sub-shape data

	// ── In-out penetration state (pointers into FPenetrationInstance or hitscan-local state) ──
	// All four MUST either all be non-null (penetration enabled) or all null (disabled). PenStatic
	// is the master switch; these pointers are only read when PenStatic != nullptr.
	float*  InOutRemainingBudget         = nullptr;
	float*  InOutCurrentDamageMultiplier = nullptr;
	int32*  InOutPenetrationCount        = nullptr;
	uint64* InOutLastPenetratedTargetId  = nullptr;

	// Optional cached AABB of target body (UE world cm). If AABBMin == AABBMax == 0 → refetch.
	FVector CachedAABBMin = FVector::ZeroVector;
	FVector CachedAABBMax = FVector::ZeroVector;
};

struct FBulletHitResult
{
	bool    bTargetKilled        = false;
	bool    bPenetrated          = false;
	bool    bRicocheted          = false;  // Incidence angle exceeded threshold → no penetration possible
	float   AppliedDamage        = 0.f;
	FVector ExitPoint            = FVector::ZeroVector;
	float   PostHitDamageMultiplier = 1.f;
};

class FATUMGAME_API UFlecsBulletHitLibrary
{
public:
	/**
	 * Resolve a single bullet impact against a target. Runs damage + crit + penetration + surface
	 * degradation + fragmentation trigger + (optional) impulse.
	 *
	 * Caller remains responsible for: velocity-falloff on penetrating projectiles, teleporting the
	 * projectile body to ExitPoint, FTagCollisionProcessed tagging, FDeathContactPoint, FTagDead /
	 * FTagDetonate on projectile, and any tracer VFX.
	 *
	 * Barrage may be nullptr if context has no penetration state AND no impulse to apply.
	 * ShooterOrProjectile: supplies IsOwnedBy check via FProjectileInstance if present.
	 */
	static FBulletHitResult ApplyBulletHit(
		flecs::world& World,
		UBarrageDispatch* Barrage,
		flecs::entity ShooterOrProjectile,
		flecs::entity Target,
		const FBulletHitContext& Ctx);
};
