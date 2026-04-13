#pragma once

#include "CoreMinimal.h"

/** Single ray-cast hit result from CastRayAllHits. Decoupled from Jolt types so
 *  callers in gameplay code don't need to include Jolt headers. */
struct FBarrageRayHit
{
	uint32  BodyIDValue      = 0;   // Jolt BodyID::GetIndexAndSequenceNumber()
	uint32  SubShapeIDValue  = 0;   // Leaf sub-shape ID for compound bodies (raw GetValue())
	FVector ContactPoint     = FVector::ZeroVector; // World-space UE coordinates (cm)
	FVector ContactNormal    = FVector::ZeroVector; // World-space UE unit vector
	float   Distance         = 0.f; // Distance from cast origin (cm)
};
