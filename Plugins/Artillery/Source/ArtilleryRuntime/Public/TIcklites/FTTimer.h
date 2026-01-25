#pragma once

#include "Ticklite.h"
#include "ArtilleryDispatch.h"
#include "Components/TimerTickliteHandlerComponent.h"

class UTimerTickliteHandlerComponent;
class UTickliteTimer;

class FTTimer : public UArtilleryDispatch::TL_ThreadedImpl
{
private:
	TWeakObjectPtr<UTimerTickliteHandlerComponent> HandlerOwner;
	
	int32 TicksRemaining;

public:
	FTTimer() : TL_ThreadedImpl()
	{
		TicksRemaining = 120;
	}

	FTTimer(UTimerTickliteHandlerComponent* Owner, int32 TickCountStart) : TL_ThreadedImpl()
	{
		HandlerOwner = Owner;
		TicksRemaining = TickCountStart;
		if (HandlerOwner.IsValid())
		{
			HandlerOwner->SetTicksRemaining(TicksRemaining);
		}
	}

	void TICKLITE_StateReset()
	{
	}

	void TICKLITE_Calculate()
	{
	}
	
	void TICKLITE_Apply() {
		TicksRemaining--;
		if (HandlerOwner.IsValid())
		{
			HandlerOwner->SetTicksRemaining(TicksRemaining);
		}
	}

	void TICKLITE_CoreReset() {
	}

	bool TICKLITE_CheckForExpiration() {
		return TicksRemaining <= 0;
	}

	void TICKLITE_OnExpiration() {
		if (HandlerOwner.IsValid())
		{
			HandlerOwner.Get()->TickliteExpired();
		}
	}
};

typedef Ticklites::Ticklite<FTTimer> TL_Timer;