#pragma once

// Radix.cpp: a fast floating-point radix sort demo
//
//   Copyright (C) Herf Consulting LLC 2001.  All Rights Reserved.
//   Use for anything you want, just tell me what you do with it.
//	 Per agreement circa 2008, adapted for gamedev by JMK
//	 Resurrected by an alignment of the stars, and now released as MIT Licensed.

// ------------------------------------------------------------------------------------------------
// ---- Basic types
#include <xmmintrin.h>	// for prefetch
#include "AtypicalDistances.h"
#include "CoreMinimal.h"
#include "HRadix.generated.h"


//This little beastie's been mutated. it takes the concatenated float key and index, as lsd sort is not stable.
//this causes the original order to be tacitly preserved, allows lookup into an existing indexed array
//and just generally makes my life suck a lot less.
USTRUCT(BlueprintType)
struct FHRadixSort
{
	GENERATED_BODY()
	typedef float real32;
	typedef unsigned char uint8;
	typedef const char* cpointer;
	#define __GRANDSLAM(x)	_mm_prefetch(cpointer(x + i + 64), 0)
	#define __L2BURNER(x)	_mm_prefetch(cpointer(x + i + 128), 0)
	struct ONUS
	{
		uint32 a;
		uint32 b;
	};


	// ---- utils for accessing 11-bit quantities
	//OMNISLASHIN' THE DAY AWAY
#define __SLICEONE(x)	(x & 0x7FF)
#define __SLICETWO(x)	(x >> 11 & 0x7FF)
#define __SLICETHREE(x)	(x >> 22  & 0x7FF)
#define __SLICEFOUR(x)	(x >> 33 & 0x7FF)
#define __SLICEFIVE(x)	(x >> 44 & 0x7FF)
#define __SLICESIX(x)	(x >> 55)

	// ================================================================================================
	// Main radix sort: expects a 64bit input composed of [float|uint32]
	// this is a completely destructive function. do not use it on anything you love.
	// it cannot tell the difference between flesh and metal.
	// ================================================================================================
	static void IncineratingRadixSort(ONUS* farray, ONUS* buffer, uint32 elements)
	{
		uint32 i;
		uint64* sortbuffer = (uint64*)buffer;

		// 6 histograms on the stack:
		const uint32 kHist = 2048;
		uint32 b0[kHist * 6];

		uint32* b1 = b0 + kHist;
		uint32* b2 = b1 + kHist;
		uint32* b3 = b2 + kHist;
		uint32* b4 = b3 + kHist;
		uint32* b5 = b4 + kHist;

		for (i = 0; i < kHist * 3; i++)
		{
			b0[i] = 0;
		}
		uint64* array = (uint64*)farray;
		// 1.  parallel histogramming pass
		//
		for (i = 0; i < elements; i++)
		{
			__GRANDSLAM(farray);
			farray[i].a = AtypicalDistances::FloatFlip(farray[i].a); //hooboy.
			auto fi = array[i];
			//OM
			b0[__SLICEONE(fi)]++;
			b1[__SLICETWO(fi)]++;
			b2[__SLICETHREE(fi)]++;
			//NOM
			b3[__SLICEFOUR(fi)]++;
			b4[__SLICEFIVE(fi)]++;
			b5[__SLICESIX(fi)]++;
		}

		// 2.  Sum the histograms -- each histogram entry records the number of values preceding itself.
		{
			uint32 sum0 = 0, sum1 = 0, sum2 = 0, sum3 = 0, sum4 = 0, sum5 = 0;
			uint32 tsum;
			for (i = 0; i < kHist; i++)
			{
				tsum = b0[i] + sum0;
				b0[i] = sum0 - 1;
				sum0 = tsum;

				tsum = b1[i] + sum1;
				b1[i] = sum1 - 1;
				sum1 = tsum;

				tsum = b2[i] + sum2;
				b2[i] = sum2 - 1;
				sum2 = tsum;

				tsum = b3[i] + sum3;
				b3[i] = sum3 - 1;
				sum3 = tsum;
				
				tsum = b4[i] + sum4;
				b4[i] = sum4 - 1;
				sum4 = tsum;

				tsum = b5[i] + sum5;
				b5[i] = sum5 - 1;
				sum5 = tsum;
			}
		}

		
		// slice 0: floatflip entire value, read/write histogram, write out flipped
		for (i = 0; i < elements; i++)
		{
			uint64 pos = __SLICEONE(array[i]);
			__L2BURNER(array);
			sortbuffer[++b0[pos]] = array[i];
		}

		// slice 1: read/write histogram, copy
		//   sorted -> array
		for (i = 0; i < elements; i++)
		{
			uint64 pos = __SLICETWO(sortbuffer[i]);
			__L2BURNER(sortbuffer);
			array[++b1[pos]] = sortbuffer[i];
		}

		// slice 2: read/write histogram, copy & flip out
		//   array -> sorted
		for (i = 0; i < elements; i++)
		{
			uint64 pos = __SLICETHREE(array[i]);
			__L2BURNER(array);
			sortbuffer[++b2[pos]] = array[i];
		}

		// slice 3: read/write histogram, copy
		//   sorted -> array
		for (i = 0; i < elements; i++)
		{
			uint64 pos = __SLICEFOUR(sortbuffer[i]);
			__L2BURNER(sortbuffer);
			array[++b3[pos]] = sortbuffer[i];
		}

		// slice 4: read/write histogram, copy & flip out
		//   array -> sorted
		for (i = 0; i < elements; i++)
		{
			uint64 pos = __SLICEFIVE(array[i]);
			__L2BURNER(array);
			sortbuffer[++b4[pos]] = array[i];
		}

		// slice 5: read/write histogram, copy
		//   sorted -> array
		for (i = 0; i < elements; i++)
		{
			uint64 pos = __SLICESIX(sortbuffer[i]);
			__L2BURNER(sortbuffer);
			array[++b5[pos]] = sortbuffer[i];
		}
		
	}
};