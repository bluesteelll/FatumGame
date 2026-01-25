#pragma once
#include "HRadix.h"
#include "CoreMinimal.h"
#include "GKSHull.generated.h"

USTRUCT()
struct FGKSHull
{
	GENERATED_BODY()


	inline double CCW(FVector2f a, FVector2f b, FVector2f c)
	{
		return ((b.X - a.X) * (c.Y - a.Y) - (b.Y - a.Y) * (c.X - a.X));
	}

	//I feel bad adding my name here but this required a bunch of fairly exotic insights to get to true linear time, key among which is that
	//you can use a "far field" trick to avoid actually needing a point in the set as the minimum and that you can radix sort the floats.
	//these operations MUST be unary.
	inline int GrahamKurzerScan(std::vector<FVector2f>& cH, std::vector<FVector2f> Points);
};
