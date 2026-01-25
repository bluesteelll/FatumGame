// No Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "CoreMinimal.h"
#include "Math/InterpCurve.h"
#include "LCM_Config.h"
#include <limits>

struct LOCOMOCORE_API FFixedAngle
{
	//fixed precision angle allowing 10 bits of denary precision and 360 degrees of integer precision
	//-368,640 to 368,640 is probably enough unique angles for Locomo's uses.
	
	FFixedAngle(): integer(0), fractional(0)
	{
		usable = false;
	}

	explicit FFixedAngle(double Unto)
	{
		integer = Unto;
		integer = integer % 360;
		Unto *= 1024;
		fractional = Unto - integer;
	}
	FFixedAngle(double Unto, bool IsUsable)
	{
		integer = Unto;
		integer = integer % 360;
		Unto *= 1024;
		fractional = Unto - integer;
		usable = IsUsable;
	}
	short integer;
	short fractional; //1024ths of a degree.
	bool usable;

};
	struct LOCOMOCORE_API FLTState
	{

	public:
		FLTState(FVector2d Velo, FFixedAngle Angle)
		{
			R=Angle;
			V=Velo;
		}
		FFixedAngle R; //Rotate Around Z after V.
		FVector2d V; //New Velocity
		static FLTState BuildNonQuakeStickForce(float Y,
													float X,
													double AdjustedSpeedLimit)
		{
			//when my monster rises from the slab, steals a lamborghini, and activates satoshi's wallet
			//I'll still tell you it was worth it.
			auto UnQuakedSticks =   RejustifiedStickMotion(FVector2d(X,Y));
			float ControlAccelerationY = FMath::Clamp(UnQuakedSticks.V.Y, -1.0, 1.0);
			float ControlAccelerationX = FMath::Clamp(UnQuakedSticks.V.X, -1.0, 1.0);
			const FVector2d ControlAccelerationNorm = FVector2d(ControlAccelerationX, ControlAccelerationY).GetSafeNormal();
			return FLTState(ControlAccelerationNorm * AdjustedSpeedLimit, FFixedAngle());
		}
		static FLTState RejustifiedStickMotion(FVector2d StickValuesFromLookup)
		{
			double angle = atan2(StickValuesFromLookup.X, StickValuesFromLookup.Y);
			double maxMagnitude = abs(StickValuesFromLookup.X) > abs(StickValuesFromLookup.Y)
			  ? 1 / sin(angle)
			  : 1 / cos(angle);
			double magnitude = abs(StickValuesFromLookup.Length() / maxMagnitude);
			return FLTState(StickValuesFromLookup.GetSafeNormal() * magnitude, FFixedAngle(angle));
		}
	};

	//calculates a throttle-like vector representing the direction of the left stick or equivalent intents

	



	///FUNCTIONS START HERE
	inline FLTState SlowBy(FVector& Velocity, double By) 
	{
		return FLTState( FVector2d(Velocity.X * By, Velocity.Y * By), FFixedAngle());
	}
	inline double EasedDotProduct(FVector3d A, FVector3d B)
	{
		auto ContactDir = (A - B);
		auto Incident = A.GetSafeNormal();
		auto ContactNorm = ContactDir.IsNearlyZero() ? FVector3d::ZeroVector : ContactDir.GetSafeNormal();
		auto InitialDot = Incident.Dot(ContactNorm);
		auto Easing = InitialDot * InitialDot; // I've NEVER seen this trick before.
		ContactNorm = (ContactNorm + (((-Incident) - ContactNorm) * Easing)).GetSafeNormal();
		auto EasedDot = A.IsNearlyZero() ? 0 : A.Dot(ContactNorm);
		return EasedDot;
	}

	inline double EasedDotProduct(FLTState A, FLTState B)
	{
		auto ContactDir = (A.V - B.V);
		auto Incident = A.V.GetSafeNormal();
		auto ContactNorm = ContactDir.IsNearlyZero() ? FVector2d::ZeroVector : ContactDir.GetSafeNormal();
		auto InitialDot = Incident.Dot(ContactNorm);
		auto Easing = InitialDot * InitialDot; // I've NEVER seen this trick before.
		ContactNorm = (ContactNorm + (((-Incident) - ContactNorm) * Easing)).GetSafeNormal();
		auto EasedDot = A.V.IsNearlyZero() ? 0 : A.V.Dot(ContactNorm);
		return EasedDot;
	}

	inline FLTState& DestructiveReduceByAtWorst(FLTState& Velo, double MinimumFactor, double RequestedFactor)
	{
		Velo.V *= FMath::Max(MinimumFactor, RequestedFactor);
		return Velo;
	}

	inline FLTState& DestructiveFastAccel(FLTState& Velo, double NormalAcceleration, double Reduction)
	{
		Velo.V *= ((2 - Reduction) * NormalAcceleration);
		return Velo;
	}

double inline HeavyYawOffsetSmoother(const float InitialAngle, const float AngleThreshold, const float KickOffset)
	{
		double ThresholdAngle = 6.390 - 0.0013 * FMath::Pow(AngleThreshold, 2.0f) + 0.61f * AngleThreshold;

		double Eproximation = 2.7183f;
		double FalloffFactor = .5f;
		return (1.0f / (1.0f + (pow(Eproximation, -(InitialAngle * FalloffFactor - ThresholdAngle))))) * KickOffset;
	}


void inline DampFloat(float& currentValue, float targetValue, float deltaTime, float increaseSpeed = 1.0f,
			   float decreaseSpeed = 1.0f)
	{
		const float difference = targetValue - currentValue;
		if (difference != 0.0f)
		{
			float signedSpeed = difference >= 0.f ? increaseSpeed : -decreaseSpeed;
			float valueDelta = signedSpeed * deltaTime;

			currentValue = signedSpeed == 0.f || abs(difference) <= abs(valueDelta)
							   ? targetValue
							   : (currentValue + valueDelta);
		}
	}
template<class SampleType=double, class AccumType=double>
class LOCOMOCORE_API BRunningAverage
{
   AccumType mTotal;
   int mHead;
   int mTail;
   int mNumSamples;
   int mRingBufferSize;
   SampleType* mpRingBuffer;

   int nextElement(int i) const
   { 
      return (i + 1) % mRingBufferSize; 
   }

   void assign(const BRunningAverage& b)
   {		
      mTotal = b.mTotal;
      mHead = b.mHead;
      mTail = b.mTail;
      mNumSamples = b.mNumSamples;
      mRingBufferSize = b.mRingBufferSize;
      if (!mpRingBuffer)
         mpRingBuffer = new SampleType[mRingBufferSize];
               
      std::copy(b.mpRingBuffer, b.mpRingBuffer + mRingBufferSize, mpRingBuffer);
   }

public:
   BRunningAverage(const BRunningAverage& b) : 
      mpRingBuffer(NULL)
   {
      assign(b);
   }

   BRunningAverage(int bufferSize = 8)
   {
      
      // One extra, because one entry is a sentinel/dummy.
      mRingBufferSize = bufferSize + 1; 
      mpRingBuffer = new SampleType[mRingBufferSize];
      
      clear();
   }

   ~BRunningAverage()
   {
      delete [] mpRingBuffer;
   }
   
   void set(int bufferSize)
   {
      if (bufferSize == mRingBufferSize)
         return;
         
      clear();
      
      mRingBufferSize = bufferSize + 1; 
      
      delete mpRingBuffer;
      mpRingBuffer = new SampleType[mRingBufferSize];
   }
   
   BRunningAverage& operator= (const BRunningAverage& b)
   {
      if (this == &b)
         return *this;

      if (mRingBufferSize < b.mRingBufferSize)
      {
         delete [] mpRingBuffer;
         mpRingBuffer = NULL;
      }

      assign(b);
      return *this;
   }
   
   void clear(void)
   {
      mTotal = 0;
      mNumSamples = mHead = mTail = 0;
      //std::fill(mpRingBuffer, mpRingBuffer + mRingBufferSize, 0);
   }

   void addSample(SampleType sample)
   {
      mTotal += (mpRingBuffer[mHead] = sample);
      mNumSamples++;

      if ((mHead = nextElement(mHead)) == mTail)
      {
         mTotal -= mpRingBuffer[mTail];
         if (mTotal < 0) 
            mTotal = 0; // due to FP rounding
         mNumSamples--;
         mTail = nextElement(mTail);
      }
   }

   AccumType getAverage(void) const
   {
      return mNumSamples ? (mTotal / static_cast<SampleType>(mNumSamples)) : 0;
   }

   int getNumSamples(void) const { return mNumSamples; }

   AccumType getTotal(void) const
   {
      return mTotal;
   }

   SampleType getMaximum(void) const
   {
      if (!mNumSamples)
         return 0;

   	//replace with optional to conform to our idioms?
      SampleType maxValue = static_cast<SampleType>(-std::numeric_limits<float>::max());
      
      for (int i = 0, j = mTail; i < mNumSamples; i++, j = nextElement(j))
         maxValue = FMath::Max(maxValue, mpRingBuffer[j]);
         
      return maxValue;
   }
   
};

class SmallArrays
{
public:
	template<class Iter, class Compare>
static inline void insertion_sort(Iter begin, Iter end, Compare comp) {
		using T = typename std::iterator_traits<Iter>::value_type;

		for (Iter cur = begin + 1; cur < end; ++cur) {
			Iter sift = cur;
			Iter sift_1 = cur - 1;

			// Compare first so we can avoid 2 moves for an element already positioned correctly.
			if (comp(*sift, *sift_1)) {
				T tmp = std::move(*sift);

				do { *sift-- = std::move(*sift_1); }
				while (sift != begin && comp(tmp, *--sift_1));

				*sift = std::move(tmp);
			}
		}
	}
	template<class Iter>
static	inline void insertion_sort(Iter begin, Iter end) {
		insertion_sort(begin, end, std::less<std::decay_t<decltype(*begin)>>());
	}
};