// Reusable cone-shaped impulse utility.
// Applies impulse to all dynamic bodies + characters within a cone.
// Sim thread only. Used by KineticBlast ability, grenades, barrel explosions, etc.

#pragma once

#include "SkeletonTypes.h"

class UBarrageDispatch;
struct FCharacterPhysBridge;

struct FConeImpulseParams
{
	FVector3d OriginUE;              // UE world position of impulse source
	FVector3d DirectionUE;           // UE world direction (normalized, cone center axis)
	float Radius = 800.f;           // cm, max reach
	float HalfAngleDeg = 45.f;     // degrees, cone half-angle (45° = 90° total cone)
	float ImpulseStrength = 1500.f; // cm/s, base impulse magnitude
	float FalloffExponent = 1.f;    // 1.0 = linear, 2.0 = quadratic, 0.0 = no falloff
	FSkeletonKey SourceKey;          // body to exclude from impulse (or self-knockback target)
	bool bAffectSelf = true;        // if true, apply self-knockback to SourceKey owner
	float SelfImpulseMultiplier = 0.5f; // scale for self-knockback (e.g. 0.5 = half push)
};

// Per-body hit result (non-character bodies only). Returned via optional OutHits.
struct FConeImpulseHit
{
	FSkeletonKey BodyKey;    // KeyOutOfBarrage for GetShapeRef lookup
	FVector ImpulseUE;       // actual impulse applied (with falloff)
	FVector BodyPositionUE;  // body center position in UE coords
};

// Apply cone-shaped impulse to all dynamic bodies + characters in range.
// MUST be called from a thread with Barrage access (sim thread, or after EnsureBarrageAccess()).
// If OutHits is non-null, appends hit data for each non-character body that received impulse.
void ApplyConeImpulse(
	const FConeImpulseParams& Params,
	UBarrageDispatch* Barrage,
	TArray<FCharacterPhysBridge>& CharacterBridges,
	TArray<FConeImpulseHit>* OutHits = nullptr);
