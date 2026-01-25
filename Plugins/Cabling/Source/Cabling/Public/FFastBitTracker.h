// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

// trackers from this bristlecone use this value.
// as the codebase grows and coheres into something truly compositionable for support of 
// K streams, particularly K received, this will likely end up a template value and this
// file will probably merge with constants into a templated or constructed class in order
// to allow it to be instanced per running bristlecone thread-set.
static constexpr uint64_t FFTID_BIT_PREFIX = 0xABBA000000000000;

class FFastBitTracker
{
public:
	enum COMPARE_RESULT {
		ID_MISMATCH = -1,
		IDENTICAL_INCLUDING_EMPTY = 0,
		LHS_SUPER_RHS = 1,
		RHS_SUPER_LHS = 2,
		DISJOINT = 3,
	};
	
	// note, because we may do bitmath on highest seen, we need to keep it as an unsigned
	// this means a few of our comparisons are odd to avoid needing signed numbers.
	uint64_t HighestSeen;
	uint64_t SeenCycles;
	uint64_t FFBTID;

	// must have a unique FFBTID per entity per stream or task.
	// comparisons between trackers are assumed non-meaningful.
	FFastBitTracker(uint32_t UNIQUE_ID_OF_TRACKED, uint64_t bit_prefix = FFTID_BIT_PREFIX)
	{
		HighestSeen = 0;
		SeenCycles = 0;
		FFBTID = bit_prefix | UNIQUE_ID_OF_TRACKED; //suffix unto the slings of outrageous prefix, right?
	}

	COMPARE_RESULT Compare(FFastBitTracker RHS)
	{
		// this can be written far more efficiently as a series of truly fast bit ops
		// but I want to make sure it's legible. normally I'd nest terns and bit ops.
		if (FFBTID != RHS.FFBTID) return ID_MISMATCH;
		if (SeenCycles == RHS.SeenCycles) return IDENTICAL_INCLUDING_EMPTY;
		uint64_t andResult = SeenCycles & RHS.SeenCycles;
		if (SeenCycles == andResult)
		{
			return LHS_SUPER_RHS;
		}
		if (RHS.SeenCycles == andResult)
		{
			return RHS_SUPER_LHS;
		}
		return DISJOINT;
	}
	
	// do not use this unless you know exactly what you are doing, and I still don't recommend it.
	COMPARE_RESULT UnsafeCompareWithoutID(FFastBitTracker RHS)
	{
		// this can ALSO be written far more efficiently as a series of truly fast bit ops
		// but I want to make sure it's legible. That's really really important here,
		// especially to make the diff obvious between the two compares.
		//  
		// This one DOES NOT CHECK ID, allowing you to compare cycles seen between two
		// different tracked streams. 
		//  
		// These streams MAY NOT SHARE CYCLE IDS!!!!
		// In fact, they PROBABLY DO NOT. Bristlecone does NOT sync cycle id across streams
		// and doing so has a lot of odd implications. However, you can force synchro if you
		// know exactly what you are doing, and this can be extremely powerful.
		// it can also be error prone, unreliable, and very slow.
		if (SeenCycles == RHS.SeenCycles) return IDENTICAL_INCLUDING_EMPTY;
		uint64_t andResult = SeenCycles & RHS.SeenCycles;
		if (SeenCycles == andResult)
		{
			return LHS_SUPER_RHS;
		}
		if (RHS.SeenCycles == andResult)
		{
			return RHS_SUPER_LHS;
		}
		return DISJOINT;
	}

	// Mutate, returns true if updated, false if bit is set OR unsettable
	bool Update(uint64_t cycle)
	{
		// explaining why this works to detect wrap arounds is quite painful
		// but basically, cycle starts as a 32 bit number, Highest always comes from cycle
		if (cycle > HighestSeen || HighestSeen - cycle > 0xFFFF)
		{
			//this isn't strictly needed but you shouldn't shift more than the width.
			cycle - HighestSeen > 64 ? SeenCycles = 0 : SeenCycles >>= (cycle - HighestSeen);
			HighestSeen = cycle;
			return true;
		}
		// if it's off the bottom of the mask by a more reasonable amount
		// we discard it. again, we use positive deltas.
		if (HighestSeen - cycle > 64)
		{
			return false;
		}
		// if we've seen it, we discard it. Also, 1ull is awful.
		if (SeenCycles & (1ull << (HighestSeen - cycle)))
		{
			return false;
		}
		// If we hadn't seen it, we set it now.
		SeenCycles |= (1ull << (HighestSeen - cycle));
		return true;
	}

	// check if we've seen a value or if it's too far in the past
	bool CheckSeenOrPast(uint64_t cycle)
	{
		if (cycle > HighestSeen)
		{
			return false;
		} 
		if (HighestSeen - cycle > 64) // lets us stay unsigned.
		{
			return true;
		}
		if (SeenCycles & 1ull << (HighestSeen - cycle))
		{
			return true;
		}
		return false;
	}
	
private:
	FFastBitTracker()
	{
		HighestSeen = 0;
		SeenCycles = 0;
		FFBTID = FFTID_BIT_PREFIX;
	}
};

// today I learned that seer is just see-er. :|
typedef FFastBitTracker CycleTracking;
