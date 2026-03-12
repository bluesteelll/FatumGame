// Stealth system components for Flecs entities.
// Static components live on PREFAB (shared by all entities of same type).
// Instance component lives on each character ENTITY (mutable per-character state).

#pragma once

#include "CoreMinimal.h"
#include "FlecsStealthTypes.h"
#include "FlecsMovementComponents.h"

DECLARE_LOG_CATEGORY_EXTERN(LogStealth, Log, All);

class UFlecsStealthLightProfile;
class UFlecsNoiseZoneProfile;

// ═══════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════

inline float GetSurfaceNoiseModifier(ESurfaceNoise Type)
{
	switch (Type)
	{
	case ESurfaceNoise::Quiet:    return 0.5f;
	case ESurfaceNoise::Normal:   return 1.0f;
	case ESurfaceNoise::Loud:     return 1.5f;
	case ESurfaceNoise::VeryLoud: return 2.0f;
	default: checkNoEntry(); return 1.0f;
	}
}

inline float GetPostureStealthModifier(uint8 Posture)
{
	switch (static_cast<ECharacterPosture>(Posture))
	{
	case ECharacterPosture::Standing:  return 1.0f;
	case ECharacterPosture::Crouching: return 0.6f;
	case ECharacterPosture::Prone:     return 0.25f;
	default: checkNoEntry(); return 1.0f;
	}
}

// ═══════════════════════════════════════════════════════════════
// CONSTANTS
// ═══════════════════════════════════════════════════════════════

namespace StealthConstants
{
	constexpr float TauLightEnter = 0.25f;
	constexpr float TauShadowEnter = 0.1f;
	constexpr float NoiseDecayRate = 0.5f;
	constexpr float DefaultAmbient = 0.1f;
	constexpr float PostureModStanding = 1.0f;
	constexpr float PostureModCrouching = 0.6f;
	constexpr float PostureModProne = 0.25f;
	constexpr float MovementModStationary = 0.15f;
	constexpr float MovementSpeedExponent = 0.7f;
	constexpr float EquipmentModDefault = 1.0f;
	constexpr float ShadowOcclusionMinDistSq = 1.0f;

	// Multi-sample Z offsets (cm, relative to entity position)
	constexpr float SampleFeetZ = 0.f;
	constexpr float SampleTorsoZ = 50.f;
	constexpr float SampleHeadZ = 90.f;
}

// ═══════════════════════════════════════════════════════════════
// NOISE EVENT (passed via TArray buffer, not a standalone Flecs component)
// ═══════════════════════════════════════════════════════════════

struct FNoiseEvent
{
	float BaseLoudness = 0.f;
	float BaseRadius = 0.f;  // cm, for future AI use
};

// ═══════════════════════════════════════════════════════════════
// WORLD POSITION (for non-physics entities: lights, zones, waypoints)
// ═══════════════════════════════════════════════════════════════

struct FWorldPosition
{
	FVector Position = FVector::ZeroVector;
};

// ═══════════════════════════════════════════════════════════════
// STEALTH LIGHT STATIC — on prefab
// ═══════════════════════════════════════════════════════════════

/**
 * Static stealth light data — lives on prefab, shared by all lights of this type.
 * Describes a gameplay-only light source for stealth illumination computation.
 */
struct FStealthLightStatic
{
	EStealthLightType Type = EStealthLightType::Point;
	float Intensity = 1.f;
	float Radius = 1000.f;

	// Spot light cone (precomputed cosines, unused for Point)
	float InnerConeAngleCos = 0.f;
	float OuterConeAngleCos = 0.f;

	/** World-space direction for Spot lights */
	FVector Direction = FVector::ForwardVector;

	static FStealthLightStatic FromProfile(const UFlecsStealthLightProfile* Profile);
};

// ═══════════════════════════════════════════════════════════════
// NOISE ZONE STATIC — on prefab
// ═══════════════════════════════════════════════════════════════

/**
 * Static noise zone data — lives on prefab, shared by all zones of this type.
 * Defines an AABB region where footstep noise is modified.
 */
struct FNoiseZoneStatic
{
	/** Half-extents in cm */
	FVector Extent = FVector(100.f, 100.f, 100.f);

	ESurfaceNoise SurfaceType = ESurfaceNoise::Normal;

	/** Check if a world-space point is inside this zone (given zone center) */
	bool ContainsPoint(const FVector& Point, const FVector& Center) const
	{
		const FVector Delta = (Point - Center).GetAbs();
		return Delta.X <= Extent.X && Delta.Y <= Extent.Y && Delta.Z <= Extent.Z;
	}

	static FNoiseZoneStatic FromProfile(const UFlecsNoiseZoneProfile* Profile);
};

// ═══════════════════════════════════════════════════════════════
// STEALTH INSTANCE — per character, mutable
// ═══════════════════════════════════════════════════════════════

/**
 * Per-character stealth state — mutable instance data.
 * Updated each sim tick by the stealth computation system.
 */
struct FStealthInstance
{
	/** Current smoothed light level [0, 1] */
	float LightLevel = 0.f;

	/** Current noise level [0, 1] (decays over time) */
	float NoiseLevel = 0.f;

	/** Final detectability [0, 1] combining light, noise, posture, movement */
	float Detectability = 0.f;

	/** Raw (unsmoothed) light level from latest sample */
	float RawLightLevel = 0.f;

	/** Pending noise events to be processed this tick */
	TArray<FNoiseEvent> PendingNoise;
};

// ═══════════════════════════════════════════════════════════════
// TAGS
// ═══════════════════════════════════════════════════════════════
// Note: FTagStealthLight and FTagNoiseZone are in FlecsGameTags.h
