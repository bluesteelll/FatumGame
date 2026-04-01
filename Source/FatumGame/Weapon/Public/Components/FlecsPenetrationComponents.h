// Penetration components for bullet pass-through mechanics.
// FPenetrationStatic lives in PREFAB (shared), FPenetrationInstance is per-entity (mutable).
// FPenetrationMaterial lives in PREFAB for target entities (walls, destructibles, etc).

#pragma once

#include "CoreMinimal.h"

// EPenetrationMaterialCategory is defined in FlecsPhysicsProfile.h (UHT-aware header).
// We forward-reference it here and include the enum for the resistance table.
#include "FlecsPhysicsProfile.h"

class UFlecsProjectileProfile;
class UFlecsPhysicsProfile;

// ═══════════════════════════════════════════════════════════════
// MATERIAL RESISTANCE LOOKUP TABLE
// 1 cache line (64 bytes), always hot during penetration checks.
// Indexed by EPenetrationMaterialCategory (0-15).
// ═══════════════════════════════════════════════════════════════

struct FMaterialProps
{
	float Resistance;
	float DegradeRate;  // Base integrity loss per standard hit (25dmg rifle)
};

static constexpr FMaterialProps GMaterialTable[16] = {
	{999.f, 0.00f},   // 0: Impenetrable
	{0.15f, 0.20f},   // 1: Flesh (degrades very fast)
	{0.2f,  0.30f},   // 2: Glass (shatters easily)
	{0.3f,  0.15f},   // 3: Drywall
	{0.5f,  0.12f},   // 4: Wood_Thin
	{1.0f,  0.08f},   // 5: Wood_Thick
	{1.5f,  0.06f},   // 6: Sheet_Metal
	{2.0f,  0.05f},   // 7: Concrete_Thin
	{3.0f,  0.03f},   // 8: Metal (degrades slow)
	{3.5f,  0.025f},  // 9: Brick
	{4.0f,  0.02f},   // 10: Concrete (degrades very slow)
	{5.0f,  0.015f},  // 11: Armor_Plate
	{8.0f,  0.01f},   // 12: Heavy_Armor
	{15.0f, 0.005f},  // 13: Reserved
	{25.0f, 0.003f},  // 14: Reserved
	{999.f, 0.00f},   // 15: Reserved
};

FORCEINLINE float GetResistanceForCategory(EPenetrationMaterialCategory Cat)
{
	return GMaterialTable[static_cast<uint8>(Cat) & 0xF].Resistance;
}

FORCEINLINE float GetDegradeRateForCategory(EPenetrationMaterialCategory Cat)
{
	return GMaterialTable[static_cast<uint8>(Cat) & 0xF].DegradeRate;
}

// ═══════════════════════════════════════════════════════════════
// PENETRATION STATIC (on projectile prefab)
// ═══════════════════════════════════════════════════════════════

/**
 * Static penetration data - lives in PREFAB, shared by all penetrating projectiles of this type.
 * Only set on entities with bPenetrating=true in ProjectileProfile.
 */
struct FPenetrationStatic
{
	/** Total penetration budget in cm of material-equivalent */
	float PenetrationBudget = 20.f;

	/** Max number of objects to penetrate (-1 = unlimited) */
	int32 MaxPenetrations = -1;

	/** Damage loss factor per budget consumed (0-2). Higher = more damage loss. */
	float DamageFalloffFactor = 1.f;

	/** Velocity loss factor per budget consumed (0-1). Higher = more speed loss. */
	float VelocityFalloffFactor = 0.5f;

	/** Minimum cos(angle) for penetration. Below this, ricochet/stop instead.
	 *  Default 0.34 ~ 70 deg from normal. */
	float RicochetCosAngleThreshold = 0.34f;

	/** Kinetic energy transfer to penetrated objects (fragmentation impulse).
	 *  0 = clean pass-through, 1 = full energy dump. */
	float ImpulseTransferFactor = 0.3f;

	static FPenetrationStatic FromProfile(const UFlecsProjectileProfile* Profile);
};

// ═══════════════════════════════════════════════════════════════
// PENETRATION INSTANCE (per projectile entity, mutable)
// ═══════════════════════════════════════════════════════════════

/**
 * Mutable penetration data - per-entity.
 * Tracks remaining budget and accumulated damage loss as the projectile passes through objects.
 */
struct FPenetrationInstance
{
	/** Remaining penetration budget */
	float RemainingBudget = 20.f;

	/** Number of objects penetrated so far */
	int32 PenetrationCount = 0;

	/** Accumulated damage multiplier (compounds with each penetration) */
	float CurrentDamageMultiplier = 1.f;

	/** Entity ID of the last penetrated target. Prevents BounceCollisionSystem from
	 *  killing the bullet on spurious re-contacts with the SAME target from the same StepWorld.
	 *  Reset to 0 when the bullet contacts a DIFFERENT entity. */
	uint64 LastPenetratedTargetId = 0;
};

// ═══════════════════════════════════════════════════════════════
// PENETRATION MATERIAL (on target entity prefab)
// ═══════════════════════════════════════════════════════════════

/**
 * Material resistance data - lives in PREFAB for target entities.
 * Determines how much penetration budget is consumed when a bullet passes through.
 * Only added when PhysicsProfile::MaterialCategory != Impenetrable (or legacy MaterialResistance > 0).
 */
struct FPenetrationMaterial
{
	/** Material category — indexes into GResistanceTable for resistance value */
	EPenetrationMaterialCategory MaterialCategory = EPenetrationMaterialCategory::Impenetrable;

	/** Minimum cos(angle) for penetration on this material.
	 *  Default 0.26 ~ 75 deg from normal. */
	float RicochetCosAngleThreshold = 0.26f;

	/** Whether this material supports cumulative surface degradation */
	bool bDegradable = true;

	/** Base integrity loss per standard hit (normalized). Higher = degrades faster. */
	float BaseDegradeRate = 0.08f;

	/** How much degradation bleeds to neighboring grid cells (0-1). */
	float DegradeSpreadFactor = 0.3f;

	/** Grid resolution override (0 = auto from AABB). */
	uint8 GridCols = 0;
	uint8 GridRows = 0;

	/** Get the resistance value for this material from the global lookup table */
	float GetResistance() const { return GetResistanceForCategory(MaterialCategory); }

	static FPenetrationMaterial FromProfile(const UFlecsPhysicsProfile* Profile);
};

// ═══════════════════════════════════════════════════════════════
// SURFACE INTEGRITY GRID
// Tracks per-cell structural damage on penetrable surfaces.
// Lazily initialized on first bullet hit.
// ═══════════════════════════════════════════════════════════════

enum class EGridProjection : uint8 { XZ = 0, XY = 1, YZ = 2 };

struct FSurfaceIntegrity
{
	uint16 Integrity[64];           // 128 bytes — 2 cache lines (HOT read+write)
	float GridOriginU = 0.f;        // projection axis 1 origin (local space)
	float GridOriginV = 0.f;        // projection axis 2 origin (local space)
	float InvCellSizeU = 1.f;       // pre-computed 1/cellWidth
	float InvCellSizeV = 1.f;       // pre-computed 1/cellHeight
	uint8 ActiveCols = 4;
	uint8 ActiveRows = 4;
	EGridProjection Projection = EGridProjection::XZ;
	uint8 _pad = 0;
	// Total: ~148 bytes

	FORCEINLINE int32 WorldToCell(const FVector& WorldPos, const FVector& BodyPos, const FQuat& BodyRot) const
	{
		const FVector Local = BodyRot.UnrotateVector(WorldPos - BodyPos);
		float U, V;
		switch (Projection)
		{
			case EGridProjection::XZ: U = Local.X; V = Local.Z; break;
			case EGridProjection::XY: U = Local.X; V = Local.Y; break;
			case EGridProjection::YZ: U = Local.Y; V = Local.Z; break;
			default: U = Local.X; V = Local.Z; break;
		}
		const int32 Col = FMath::Clamp(static_cast<int32>((U - GridOriginU) * InvCellSizeU), 0, ActiveCols - 1);
		const int32 Row = FMath::Clamp(static_cast<int32>((V - GridOriginV) * InvCellSizeV), 0, ActiveRows - 1);
		return Row * 8 + Col;  // Fixed stride 8 for cache alignment
	}

	FORCEINLINE float GetIntegrity(int32 Idx) const { return Integrity[Idx] / 65535.f; }

	FORCEINLINE void DegradeCell(int32 Idx, uint16 Amount)
	{
		Integrity[Idx] = (Amount >= Integrity[Idx]) ? 0 : (Integrity[Idx] - Amount);
	}

	void DegradeWithSpread(int32 CenterIdx, float NormalizedAmount, float SpreadFactor);

	/** Initialize grid from body AABB. AABBMin/AABBMax in UE local coords. */
	void InitFromAABB(const FVector& AABBMin, const FVector& AABBMax, uint8 DesiredCols = 0, uint8 DesiredRows = 0);
};

// ═══════════════════════════════════════════════════════════════
// INTEGRITY → RESISTANCE MODIFIER
// Maps cell integrity [0,1] to a resistance multiplier [0,1].
// Piecewise: full resistance above knee, rapid degradation below.
// ═══════════════════════════════════════════════════════════════

FORCEINLINE float IntegrityToResistance(float IntegrityValue)
{
	constexpr float Knee = 0.3f;
	constexpr float MinFactor = 0.15f;
	if (IntegrityValue >= Knee)
	{
		const float T = (IntegrityValue - Knee) / (1.f - Knee);
		return FMath::Lerp(MinFactor, 1.f, T);
	}
	return MinFactor * (IntegrityValue / Knee);
}

// FTagCollisionPenetration is defined in FlecsBarrageComponents.h alongside other collision tags
