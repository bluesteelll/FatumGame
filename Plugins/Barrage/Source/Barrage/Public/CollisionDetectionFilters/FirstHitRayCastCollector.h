#pragma once
#include "IsolatedJoltIncludes.h"

class FirstHitRayCastCollector : public JPH::RayCastBodyCollector
{
public:
	FirstHitRayCastCollector(const JPH::RRayCast &inRay, JPH::RayCastResult &ioHit, const JPH::BodyLockInterface &inBodyLockInterface, const JPH::BodyFilter &inBodyFilter) :
		mRay(inRay),
		mHit(ioHit),
		mBodyLockInterface(inBodyLockInterface),
		mBodyFilter(inBodyFilter)
	{
		mContactPosition = JPH::RVec3();
	}

	virtual void AddHit(const ResultType &inResult) override
	{
		JPH_ASSERT(inResult.mFraction < mHit.mFraction, "This hit should not have been passed on to the collector");

		// Only test shape if it passes the body filter
		if (mBodyFilter.ShouldCollide(inResult.mBodyID))
		{
			// Lock the body
			JPH::BodyLockRead lock(mBodyLockInterface, inResult.mBodyID);
			if (lock.SucceededAndIsInBroadPhase()) // Race condition: body could have been removed since it has been found in the broadphase, ensures body is in the broadphase while we call the callbacks
			{
				const JPH::Body &body = lock.GetBody();

				// Check body filter again now that we've locked the body
				if (mBodyFilter.ShouldCollideLocked(body))
				{
					// Collect the transformed shape
					JPH::TransformedShape ts = body.GetTransformedShape();

					// Release the lock now, we have all the info we need in the transformed shape
					lock.ReleaseLock();

					// Do narrow phase collision check
					if (ts.CastRay(mRay, mHit))
					{
						// Test that we didn't find a further hit by accident
						JPH_ASSERT(mHit.mFraction >= 0.0f && mHit.mFraction < GetEarlyOutFraction());

						// Update early out fraction based on narrow phase collector
						UpdateEarlyOutFraction(mHit.mFraction);
						mContactPosition = mRay.GetPointOnRay(inResult.mFraction);
					}
				}
			}
		}
	}
	
	JPH::RRayCast					mRay;
	JPH::RayCastResult&				mHit;
	JPH::RVec3						mContactPosition;
	const JPH::BodyLockInterface&	mBodyLockInterface;
	
	const JPH::BodyFilter&			mBodyFilter;
};
