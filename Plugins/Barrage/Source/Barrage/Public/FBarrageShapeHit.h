#pragma once

#include "CoreMinimal.h"

/** Single shape-cast hit result from CastCapsuleAllHits (and future shape-cast variants).
 *  Decoupled from Jolt types so callers in gameplay code don't need to include Jolt headers. */
struct FBarrageShapeHit
{
	uint32  BodyIDValue      = 0;   // Jolt BodyID::GetIndexAndSequenceNumber()
	uint32  SubShapeIDValue  = 0;   // Leaf sub-shape ID for compound bodies (raw GetValue())
	FVector ImpactPoint      = FVector::ZeroVector; // World-space UE coordinates (cm)
	FVector ImpactNormal     = FVector::ZeroVector; // World-space UE unit vector
	float   Fraction         = 0.f; // Fraction along sweep [0,1]
};
