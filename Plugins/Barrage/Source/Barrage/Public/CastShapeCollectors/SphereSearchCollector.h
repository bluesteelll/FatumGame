#pragma once
#include "IsolatedJoltIncludes.h"

class SphereSearchCollector : public JPH::CollideShapeBodyCollector
{
public:
	SphereSearchCollector(const JPH::BodyLockInterface &inBodyLockInterface, const JPH::BodyFilter &inBodyFilter)
		: mBodyLockInterface(inBodyLockInterface), mBodyFilter(inBodyFilter)
	{
		BodyCount = 0;
		mBodies.Init(nullptr, MAX_FOUND_OBJECTS);
	}
	
	virtual void AddHit(const ResultType &inResult) override
	{
		if (BodyCount < MAX_FOUND_OBJECTS && mBodyFilter.ShouldCollide(inResult))
		{
			JPH::BodyLockRead lock(mBodyLockInterface, inResult);
			if (lock.SucceededAndIsInBroadPhase())
			{
				const JPH::Body &body = lock.GetBody();
				if (mBodyFilter.ShouldCollideLocked(body))
				{
					mBodies[BodyCount] = &lock.GetBody();
					BodyCount++;
				}
			}
		}
	}

	// Physics data handlers
	const JPH::BodyLockInterface& mBodyLockInterface;
	const JPH::BodyFilter& mBodyFilter;

	// Hit results
	uint32 BodyCount;
	TArray<const JPH::Body*> mBodies;
};
