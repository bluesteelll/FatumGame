#pragma once
#include "IsolatedJoltIncludes.h"

/** Narrow-phase multi-hit ray collector. Stores every sub-shape hit (body + sub-shape + fraction)
 *  for later conversion to world-space contact point/normal by the caller. Body filtering is
 *  handled by NarrowPhaseQuery::CastRay via the BodyFilter argument, so we do NOT re-filter here. */
class AllHitsRayCastCollector : public JPH::CastRayCollector
{
public:
	struct FRawHit
	{
		JPH::BodyID     BodyID;
		JPH::SubShapeID SubShapeID;
		float           Fraction;
	};

	// TInlineAllocator keeps typical shots (<16 penetrations) off the heap.
	TArray<FRawHit, TInlineAllocator<16>> Hits;

	virtual void AddHit(const ResultType& inResult) override
	{
		FRawHit& Out = Hits.AddDefaulted_GetRef();
		Out.BodyID     = inResult.mBodyID;
		Out.SubShapeID = inResult.mSubShapeID2;
		Out.Fraction   = inResult.mFraction;
		// Do NOT update early-out fraction — we want all hits.
	}
};
