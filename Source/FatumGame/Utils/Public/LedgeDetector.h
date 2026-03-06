// Stateless 5-phase ledge detection algorithm using Barrage (Jolt) raycasts.
// Used by PrepareCharacterStep for vault, mantle, and ledge grab detection.
//
// Phase 1: Forward wall check (SphereCast)
// Phase 2: High forward probe — reject walls taller than MaxReachHeight (SphereCast)
// Phase 3: Downward surface — find ledge top (SphereCast)
// Phase 4: Depth check — reject thin ledges (CastRay)
// Phase 5: Top clearance — can character stand on ledge? (CastRay)

#pragma once

#include "CoreMinimal.h"
#include "SkeletonTypes.h"

class UBarrageDispatch;
struct FMovementStatic;

/** Cached result of the 5-phase ledge detection algorithm. */
struct FLedgeCandidate
{
	FVector LedgeTopPoint = FVector::ZeroVector;   // UE world
	FVector WallHitPoint = FVector::ZeroVector;    // UE world
	FVector WallNormal = FVector::ForwardVector;   // UE world, unit
	float LedgeHeight = 0.f;                       // cm above character feet
	bool bCanPullUp = false;
	bool bValid = false;
};

/** Stateless 5-phase ledge detection using Barrage raycasts. */
class FLedgeDetector
{
public:
	/** Run full 5-phase detection. Result written to OutCandidate.
	 *  @param CharFeetPos       Character feet position (UE world, cm)
	 *  @param LookDir           Camera look direction (UE world)
	 *  @param CapsuleRadius     Character capsule radius (cm)
	 *  @param CapsuleHalfHeight Character capsule half height (cm)
	 *  @param MaxReachHeight    Maximum ledge height above feet (cm)
	 *  @param MS                Movement static params (sim-thread safe)
	 *  @param Barrage           Barrage dispatch for raycasts
	 *  @param CharBarrageKey    Character's Barrage key (to ignore own body)
	 *  @param OutCandidate      Detection result */
	static void Detect(const FVector& CharFeetPos, const FVector& LookDir,
	                   float CapsuleRadius, float CapsuleHalfHeight,
	                   float MaxReachHeight, const FMovementStatic* MS,
	                   UBarrageDispatch* Barrage, FSkeletonKey CharBarrageKey,
	                   FLedgeCandidate& OutCandidate);
};
