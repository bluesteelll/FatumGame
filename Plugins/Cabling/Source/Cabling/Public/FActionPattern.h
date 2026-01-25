// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActionPatternKey.h"
#include "Templates/SubclassOf.h"
#include "UObject/UnrealType.h"
#include "Engine/DataTable.h"
#include "MatchableTagTypes.h"

#include "ArtilleryCommonTypes.h"
#include "Containers/CircularBuffer.h"
#include "FArtilleryNoGuaranteeReadOnly.h"

//this is vulnerable to memoization but I can't think of a pretty way to do that which doesn't make rollback insane to debug.
//as a result, these lil fellers are stateless. If you wanna do a memoized version, I recommend it strongly, but make sure profiling
//shows that's actually necessary. I sincerely doubt it will be.

class FActionPattern_InternallyStateless
{
public:
	virtual uint32_t const runPattern(uint64_t frameToRunBackFrom, FActionBitMask& ToSeekUnion,FANG_PTR Buffer) const = 0;

	virtual ArtIPMKey const getName() const = 0;
	static constexpr ArtIPMKey Name = ArtIPMKey::InternallyStateless; //you should never see this as getName is virtual.
};

typedef FActionPattern_InternallyStateless FActionPattern;


class FActionPattern_SingleFrameFire : public FActionPattern_InternallyStateless
{
public:
	virtual uint32_t const runPattern(uint64_t frameToRunBackFrom,FActionBitMask& ToSeekUnion,FANG_PTR Buffer) const override
	{
		return Buffer->peek(frameToRunBackFrom)->GetButtonsAndEventsFlat() & ToSeekUnion.getFlat();
	}
	
	virtual const ArtIPMKey getName() const override { return Name; };
	static constexpr ArtIPMKey Name = ArtIPMKey::SingleFrameFire;
};

class FActionPattern_ButtonHoldAllowOneMiss : public FActionPattern_InternallyStateless
{
public:
	// returned pattern will tell us which inputs (button/events) were held
	virtual uint32_t const runPattern(uint64_t frameToRunBackFrom, FActionBitMask& ToSeekUnion, FANG_PTR Buffer) const override
	{
		/*
		  to allow for one missed input in each input bit:
		  tracker = mask
		   for x in input
			outcome = mask & x
			mask = tracker | outcome
			tracker &= outcome*/
		
		uint64_t StartIndex = FMath::Max(frameToRunBackFrom - ArtilleryHoldSweepBack, static_cast<uint64_t>(0));
		uint32_t toSeek = ToSeekUnion.getFlat();
		uint32_t tracker = toSeek;
		uint32_t outcome = 0;

		// std::optional<FArtilleryShell>(CurrentHistory[input])
		for (uint64_t i = StartIndex; i <= frameToRunBackFrom; ++i)
		{
			uint32_t x = Buffer->peek(i)->GetButtonsAndEventsFlat();
			outcome = toSeek & x;
			toSeek = tracker | outcome;
			tracker &= outcome;
		}
		// allows for 1 missed input for each input across the time frame, but still count as hold
		// this implementation does not track where in the sequence the drops were
		return outcome;
	}
	
	virtual const ArtIPMKey getName() const override { return Name; };
	static constexpr ArtIPMKey Name = ArtIPMKey::ButtonHoldAllowOneMiss;
};

class FActionPattern_OnPress : public FActionPattern_InternallyStateless
{
public:
	// returned pattern will tell us which inputs (button/events) were held
	virtual uint32_t const runPattern(uint64_t frameToRunBackFrom,FActionBitMask& ToSeekUnion, FANG_PTR Buffer) const override
	{
		uint64_t StartIndex = FMath::Max(frameToRunBackFrom - ArtilleryHoldSweepBack, static_cast<uint64_t>(0));
		uint32_t toSeek = ToSeekUnion.getFlat();

		//do NOT check current frame (< instead of <=)
		for (uint64_t i = StartIndex; i < frameToRunBackFrom; ++i)
		{
			toSeek = (Buffer->peek(i)->GetButtonsAndEventsFlat() ^ toSeek) & toSeek;
		}
		
		// this implementation does not track where in the sequence the drops were
		return toSeek & (Buffer->peek(frameToRunBackFrom)->GetButtonsAndEventsFlat() & ToSeekUnion.getFlat());
	}
	
	virtual const ArtIPMKey getName() const override { return Name; };
	static constexpr ArtIPMKey Name = ArtIPMKey::OnPress;
};

class FActionPattern_ButtonHold : public FActionPattern_InternallyStateless
{
public:
	// returned pattern will tell us which inputs (button/events) were held
	virtual uint32_t const runPattern(uint64_t frameToRunBackFrom, FActionBitMask& ToSeekUnion, FANG_PTR Buffer) const override
	{
		uint64_t StartIndex = FMath::Max(frameToRunBackFrom - ArtilleryHoldSweepBack, static_cast<uint64_t>(0));
		uint32_t toSeek = ToSeekUnion.getFlat();
		
		for (uint64_t i = StartIndex; i <= frameToRunBackFrom; ++i)
		{
			uint32_t x = Buffer->peek(i)->GetButtonsAndEventsFlat();
			toSeek = toSeek & x;
		}
		
		// this implementation does not track where in the sequence the drops were
		return toSeek;
	}
	
	virtual const ArtIPMKey getName() const override { return Name; };
	static constexpr ArtIPMKey Name = ArtIPMKey::ButtonHold;
};

class FActionPattern_ButtonReleaseNoDelay : public FActionPattern_ButtonHold 
{
public:
	virtual uint32_t const runPattern(uint64_t frameToRunBackFrom,
		FActionBitMask& ToSeekUnion,
		FANG_PTR Buffer
	)
		const override
	{
		// held before this frame?
		bool heldBefore = FActionPattern_ButtonHold::runPattern(frameToRunBackFrom - 1, ToSeekUnion, Buffer) == ToSeekUnion.getFlat();
		// not held this frame?
		bool releasedNow = (Buffer->peek(frameToRunBackFrom)->GetButtonsAndEventsFlat() & ToSeekUnion.getFlat()) == 0;
		// release is held -> not held
		return heldBefore && releasedNow ? ToSeekUnion.getFlat() : 0;
	}
	
	virtual const ArtIPMKey getName() const override { return Name; };
	static constexpr ArtIPMKey Name = ArtIPMKey::ButtonReleaseNoDelay;
};

//NOTE: if you want to check if buttons were held across the whole stick-flick
//you will need to do that separately or create a new pattern. The flick is ALREADY
//expensive. This also works a bit differently from the version found down in cabling.
//okay, a fair bit.
//NOTE THIS USES THE FLICK SWEEPBACK which is INCLUSIVE
class FActionPattern_StickFlick : public FActionPattern_InternallyStateless
{
public:
	virtual uint32_t const runPattern(uint64_t frameToRunBackFrom, FActionBitMask& ToSeekUnion, FANG_PTR Buffer) const override
	{
		//NOTE THIS USES THE FLICK SWEEPBACK which is INCLUSIVE
		std::optional<FArtilleryShell> cur = Buffer->peek(frameToRunBackFrom);
		
		//for a VARIETY OF REASONS we really don't want to start detecting flicks early.
		if(frameToRunBackFrom - ArtilleryFlickSweepBack < ArtilleryFlickSweepBack)
		{
			return 0;
		}
		
		uint64_t FinishIndex = frameToRunBackFrom - ArtilleryFlickSweepBack;
		//we never turn them into floats here.
		int32_t curX = cur->GetStickLeftXAsACSN(); 
		int32_t curY = cur->GetStickLeftYAsACSN();
		if (MatchingTools::FlickDetect<FANG_PTR>(curX, curY, frameToRunBackFrom, FinishIndex, Buffer)) {
			//AND it sweeps backward, not forward.
			return ToSeekUnion.getFlat();
		}
		return 0; //return results
	}
	
	const ArtIPMKey getName() const override { return Name; };
	static constexpr ArtIPMKey Name = ArtIPMKey::StickFlick;
};

namespace Arty
{
	namespace IPM
	{
		typedef const FActionPattern* CanonPattern;
		constexpr FActionPattern_StickFlick Flick;
		constexpr CanonPattern GFlick = &Flick;
		constexpr FActionPattern_ButtonReleaseNoDelay Release;
		constexpr CanonPattern GRelease = &Release;
		constexpr FActionPattern_SingleFrameFire FramePress;
		constexpr CanonPattern GPress = &FramePress;
		constexpr FActionPattern_ButtonHold Hold;
		constexpr CanonPattern GHold = &Hold;
		constexpr FActionPattern_ButtonHoldAllowOneMiss HoldWMiss;
		constexpr CanonPattern GHoldWM = &HoldWMiss;
		constexpr FActionPattern_OnPress StartOfPress;
		constexpr CanonPattern GPerPress = &StartOfPress;
	}
}
