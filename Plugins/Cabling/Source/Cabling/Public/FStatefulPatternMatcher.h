// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

#include "FFastBitTracker.h"
#include "MatchableTagTypes.h"
#include "FCablePackedInput.h"
#include "FMasks.h"

// these ended up being less useful for what I'm doing immediately, funnily enough.
// but i'd already written them and debugged them. We'll get use out of them.
// THESE CANNOT SAFELY BE USED IN ARTILLERY WITHOUT ANOTHER HOUR OR SO OF WORK
// IF YOU ADD THAT API SPECIFIER IN HERE, YOU DO IT AT YOUR OWN RISK
class /* are you sure?*/ FStatefulIntentTracker
{
	static constexpr uint64_t FFCABLE_BIT_PREFIX = 0x0000000000000000; //This is a special and very funny case! Don't change this.
	FFastBitTracker Memory;
	
	FStatefulIntentTracker(Arty::Intents::Intent IntentBits): Memory(IntentBits, FFCABLE_BIT_PREFIX)
	{
	}
	
	bool CheckInputWithPatternEnsureNew(uint64_t PackedInput, uint64_t cycle)
	{
		// this tracks the last 64 times a simple bit pattern has come up.
		return (PackedInput & Memory.FFBTID) == Memory.FFBTID && Memory.Update(cycle);
	}

	// Only the same patterns can be compared, but this lets you build complex functionality like the stateless matchers
	// very very easily. We can afford state because we are "below" the rollback boundary down in cabling.
	FFastBitTracker::COMPARE_RESULT Compare(FStatefulIntentTracker& other)
	{
		// this does not pass by ref, which is fine.
		return Memory.Compare(other.Memory);
	}
};

template<int width>
class TSimpleInputRing : public MatchingTools::FTrueMinimumNoStandards<FCableInputPacker>
{
public:
	virtual ~TSimpleInputRing() = default;
	uint64_t highestInput = 0;
	FCableInputPacker Ring[width];
	
	TSimpleInputRing()
	{
	}

	// yeah, it's pretty brain-dead. it does have simple in the name.
	// it is FAST though.
	virtual void add(uint64_t input, FCableInputPacker ToAdd) 
	{
		Ring[input%width] = ToAdd;
		highestInput = input > highestInput ? input : highestInput;
	}
	
	virtual std::optional<FCableInputPacker> peek(uint64_t input) override
	{
		// until it's full, the ring won't let you peek.
		// you can't stare into the future, either.
		if(highestInput > width && input <= highestInput)
		{
			return Ring[input%width];
		}
		return std::optional<FCableInputPacker>();
	}
};
