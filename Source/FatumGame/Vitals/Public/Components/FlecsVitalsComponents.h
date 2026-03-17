// Vitals system components for Flecs entities (hunger, thirst, warmth).
// Static components live on PREFAB (shared by all entities of same type).
// Instance component lives on each character ENTITY (mutable per-character state).

#pragma once

#include "CoreMinimal.h"
#include "FlecsVitalsComponents.generated.h"

class UFlecsVitalsProfile;
class UFlecsVitalsItemProfile;
class UFlecsTemperatureZoneProfile;

// ═══════════════════════════════════════════════════════════════
// ENUMS (pure C++ — not used in UPROPERTY)
// ═══════════════════════════════════════════════════════════════

enum class EVitalSeverity : uint8
{
	OK       = 0,
	Low      = 1,
	Critical = 2,
	Lethal   = 3
};

// ═══════════════════════════════════════════════════════════════
// VITAL THRESHOLD — pure C++ struct (NO GENERATED_BODY)
// ═══════════════════════════════════════════════════════════════

/**
 * Threshold boundary for a vital (hunger, thirst, warmth).
 * When the vital percent drops BELOW this Percent, the modifiers activate.
 * Array index maps to EVitalSeverity: [0]=OK, [1]=Low, [2]=Critical, [3]=Lethal.
 */
struct FVitalThreshold
{
	/** Threshold boundary (0.0–1.0). Below this, these modifiers apply. 0.0 = disabled (never matches). */
	float Percent = 0.f;

	/** Movement speed multiplier at this severity */
	float SpeedMultiplier = 1.f;

	/** HP drain per second at this severity */
	float HPDrainPerSecond = 0.f;

	/** Can the player sprint at this severity? */
	bool bCanSprint = true;

	/** Can the player jump at this severity? */
	bool bCanJump = true;

	/** Multiplier for cross-vital drain (e.g., cold increases hunger drain) */
	float CrossVitalDrainMultiplier = 1.f;
};

// ═══════════════════════════════════════════════════════════════
// VITALS STATIC — on prefab (NO GENERATED_BODY)
// ═══════════════════════════════════════════════════════════════

/**
 * Static vitals data — lives on prefab, shared by all characters of this type.
 * Defines drain rates, thresholds, and starting values.
 */
struct FVitalsStatic
{
	/** Hunger drain per second (default ~5min to empty) */
	float HungerDrainRate = 0.00333f;

	/** Thirst drain per second (default ~3.3min to empty) */
	float ThirstDrainRate = 0.005f;

	/** Threshold arrays indexed by EVitalSeverity (OK, Low, Critical, Lethal) */
	FVitalThreshold HungerThresholds[4];
	FVitalThreshold ThirstThresholds[4];
	FVitalThreshold WarmthThresholds[4];

	/** How fast warmth lerps toward target temperature (percent/sec) */
	float WarmthTransitionSpeed = 0.05f;

	/** Starting vital percentages (0.0–1.0) */
	float StartingHunger = 1.f;
	float StartingThirst = 1.f;
	float StartingWarmth = 1.f;

	static FVitalsStatic FromProfile(const UFlecsVitalsProfile* Profile);
};

// ═══════════════════════════════════════════════════════════════
// VITALS INSTANCE — per character, mutable (USTRUCT for Flecs CoW)
// ═══════════════════════════════════════════════════════════════

/**
 * Per-character vitals state — mutable instance data.
 * Updated each sim tick by the vitals drain system.
 */
USTRUCT(BlueprintType)
struct FVitalsInstance
{
	GENERATED_BODY()

	/** Current hunger level (0.0 = starving, 1.0 = full) */
	UPROPERTY(BlueprintReadOnly, Category = "Vitals")
	float HungerPercent = 1.f;

	/** Current thirst level (0.0 = dehydrated, 1.0 = full) */
	UPROPERTY(BlueprintReadOnly, Category = "Vitals")
	float ThirstPercent = 1.f;

	/** Current warmth level (0.0 = freezing, 1.0 = warm) */
	UPROPERTY(BlueprintReadOnly, Category = "Vitals")
	float WarmthPercent = 1.f;

	/** Fractional drain accumulator for hunger (sub-tick precision) */
	float HungerAccum = 0.f;

	/** Fractional drain accumulator for thirst (sub-tick precision) */
	float ThirstAccum = 0.f;

	/** Target warmth from environment zones (lerped toward each tick) */
	float TargetWarmth = 1.f;

	/** Dirty flag — re-scan inventory for passive vitals effects */
	bool bEquipmentDirty = true;
};

// ═══════════════════════════════════════════════════════════════
// STAT MODIFIERS — per character, computed from thresholds (NO GENERATED_BODY)
// ═══════════════════════════════════════════════════════════════

/**
 * Composite stat modifiers derived from current vitals severity.
 * Recomputed each tick by VitalModifierRecalcSystem.
 */
struct FStatModifiers
{
	float SpeedMultiplier = 1.f;
	bool bCanSprint = true;
	bool bCanJump = true;
	float HPDrainPerSecond = 0.f;

	/** Fractional HP drain accumulator (carries between frames) */
	float HPDrainAccum = 0.f;
};

// ═══════════════════════════════════════════════════════════════
// EQUIPMENT VITALS CACHE — per character (NO GENERATED_BODY)
// ═══════════════════════════════════════════════════════════════

/**
 * Cached passive vitals bonuses from equipped/inventory items.
 * Recomputed when bEquipmentDirty is set on FVitalsInstance.
 */
struct FEquipmentVitalsCache
{
	float WarmthBonus = 0.f;
	float HungerDrainMult = 1.f;
	float ThirstDrainMult = 1.f;
};

// ═══════════════════════════════════════════════════════════════
// VITALS ITEM STATIC — on item prefab (NO GENERATED_BODY)
// ═══════════════════════════════════════════════════════════════

/**
 * Item vitals data — lives on item prefab.
 * Defines one-shot restoration and passive inventory bonuses.
 */
struct FVitalsItemStatic
{
	/** One-shot restoration amounts (0.0–1.0 of max) */
	float HungerRestore = 0.f;
	float ThirstRestore = 0.f;
	float WarmthRestore = 0.f;

	/** Passive bonuses while item is in inventory */
	float PassiveWarmthBonus = 0.f;
	float PassiveHungerDrainMult = 1.f;
	float PassiveThirstDrainMult = 1.f;

	bool HasConsumableEffect() const
	{
		return HungerRestore > 0.f || ThirstRestore > 0.f || WarmthRestore > 0.f;
	}

	bool HasPassiveEffect() const
	{
		return PassiveWarmthBonus > 0.f
			|| !FMath::IsNearlyEqual(PassiveHungerDrainMult, 1.f)
			|| !FMath::IsNearlyEqual(PassiveThirstDrainMult, 1.f);
	}

	static FVitalsItemStatic FromProfile(const UFlecsVitalsItemProfile* Profile);
};

// ═══════════════════════════════════════════════════════════════
// CHARACTER INVENTORY REF — per character (NO GENERATED_BODY)
// ═══════════════════════════════════════════════════════════════

/**
 * Reference to this character's main inventory container entity.
 * Used by EquipmentModifierSystem to scan for passive vitals items.
 */
struct FCharacterInventoryRef
{
	int64 InventoryEntityId = 0;
};

// ═══════════════════════════════════════════════════════════════
// TEMPERATURE ZONE STATIC — on zone entity prefab (NO GENERATED_BODY)
// ═══════════════════════════════════════════════════════════════

/**
 * Static temperature zone data — lives on prefab.
 * Defines an AABB region with an ambient temperature value.
 * Same AABB containment pattern as FNoiseZoneStatic.
 */
struct FTemperatureZoneStatic
{
	/** Half-extents in cm */
	FVector Extent = FVector(200.f, 200.f, 200.f);

	/** Temperature value (0.0 = freezing, 1.0 = warm) */
	float Temperature = 0.5f;

	/** Check if a world-space point is inside this zone (given zone center) */
	bool ContainsPoint(const FVector& Point, const FVector& Center) const
	{
		const FVector Delta = (Point - Center).GetAbs();
		return Delta.X <= Extent.X && Delta.Y <= Extent.Y && Delta.Z <= Extent.Z;
	}

	static FTemperatureZoneStatic FromProfile(const UFlecsTemperatureZoneProfile* Profile);
};

// ═══════════════════════════════════════════════════════════════
// TAGS
// ═══════════════════════════════════════════════════════════════
// Note: FTagTemperatureZone is in FlecsGameTags.h
