// Reusable radial explosion utility.
// Applies damage + impulse to all dynamic bodies + characters within a sphere.
// Sim thread only. Used by ExplosionSystem, barrel destruction, abilities, etc.

#pragma once

#include "SkeletonTypes.h"
#include "GameplayTagContainer.h"
#include "flecs.h"

class UBarrageDispatch;
struct FCharacterPhysBridge;

struct FExplosionParams
{
	FVector EpicenterUE;              // UE world position of explosion center
	float Radius = 500.f;            // cm, blast radius
	float BaseDamage = 100.f;        // Base damage at epicenter
	float ImpulseStrength = 2000.f;  // cm/s, base radial impulse
	float DamageFalloff = 1.f;       // 1.0=linear, 2.0=quadratic, 0.0=flat
	float ImpulseFalloff = 1.f;      // Falloff exponent for impulse
	float VerticalBias = 0.3f;       // Upward impulse bias (0=pure radial, 0.5=moderate lift)
	uint64 OwnerEntityId = 0;        // Entity that caused the explosion (for self-damage prevention)
	bool bDamageOwner = false;        // Can hurt owner?
	FGameplayTag DamageType;          // Damage type for resistances
};

// Apply radial explosion: damage + impulse to all bodies and characters in radius.
// MUST be called from a thread with Barrage access (sim thread).
// `World` is the Flecs world for entity lookups and damage queuing.
void ApplyExplosion(
	const FExplosionParams& Params,
	UBarrageDispatch* Barrage,
	TArray<FCharacterPhysBridge>& CharacterBridges,
	flecs::world& World);
