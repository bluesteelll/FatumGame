#pragma once

#include "TL_Automation.h"

class FTAutoMortarCadenceTicklite : public CadencedTicklite
{
	uint32_t CadenceOverride = 120;
public:
	FTAutoMortarCadenceTicklite(uint32_t MyTicksToApply, const FGunKey& AutomateThisGun, uint32_t MyCadence = AUTO_MORTAR_REFIRE_CHECK_TICK_COUNT)
		: CadencedTicklite(MyTicksToApply, AutomateThisGun, MyCadence)
	{
	}

	FTAutoMortarCadenceTicklite() : FTAutoMortarCadenceTicklite(0, Default, 0)
	{
	}
	
	void TICKLITE_StateReset()
	{
	}

	void TICKLITE_Calculate()
	{
		ShouldFireMortar = ShouldTickCadencedComponents();
	}

	void TICKLITE_Apply()
	{
		if (ShouldFireMortar)
		{
			ArtilleryDispatch->RequestRouter->GunFired(MyGunToFire, ArtilleryDispatch->GetShadowNow());
		}
		
		if (TicksRemaining <= 0)
		{
			SetCadence(CadenceOverride);
		}
		--TicksRemaining;
	}

	void TICKLITE_CoreReset()
	{
	}

	bool TICKLITE_CheckForExpiration()
	{
		// TODO - stop firing when the player is dead
		return false;
	}

	void TICKLITE_OnExpiration()
	{
	}

	static constexpr uint16 AUTO_MORTAR_REFIRE_CHECK_TICK_COUNT = 480;
private:
	bool ShouldFireMortar = false;
	
	static const inline FGunKey Default = FGunKey("DefaultAutoGun");
};

using TL_AutoMortarCadenceTicklite = Ticklites::Ticklite<FTAutoMortarCadenceTicklite>;
