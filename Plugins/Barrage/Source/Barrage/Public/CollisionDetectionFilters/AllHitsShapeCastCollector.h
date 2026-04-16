#pragma once
#include "IsolatedJoltIncludes.h"

/** Narrow-phase multi-hit shape collector. Stores every sub-shape hit (body + sub-shape + fraction
 *  + penetration axis + contact point) for later conversion to world-space impact data by the caller.
 *  Body filtering is handled by NarrowPhaseQuery::CastShape via the BodyFilter argument, so we do
 *  NOT re-filter here. Mirror of AllHitsRayCastCollector but for the swept-shape path. */
class AllHitsShapeCastCollector : public JPH::CastShapeCollector
{
public:
	struct FRawHit
	{
		JPH::BodyID     BodyID;
		JPH::SubShapeID SubShapeID;
		float           Fraction;
		JPH::Vec3       ContactPointOn2;   // In cast-start local-translated space; caller offsets by start.
		JPH::Vec3       PenetrationAxis;   // Jolt separation axis (unnormalized)
	};

	// TInlineAllocator keeps typical sweeps (<16 bodies) off the heap.
	TArray<FRawHit, TInlineAllocator<16>> Hits;

	virtual void AddHit(const ResultType& inResult) override
	{
		FRawHit& Out = Hits.AddDefaulted_GetRef();
		Out.BodyID          = inResult.mBodyID2;
		Out.SubShapeID      = inResult.mSubShapeID2;
		Out.Fraction        = inResult.mFraction;
		Out.ContactPointOn2 = inResult.mContactPointOn2;
		Out.PenetrationAxis = inResult.mPenetrationAxis;
		// Do NOT update early-out fraction — we want all hits.
	}
};
