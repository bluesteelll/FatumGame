
#pragma once
#include "Ticklite.h"
#include "ArtilleryDispatch.h"
#include "FArtilleryTicklitesThread.h"

	//A ticklite's impl component(s) must provide:
	//TICKLITE_StateReset on the memory block aspect
	//TICKLITE_Calculate on the impl aspect
	//TICKLITE_Apply on the impl aspect, consuming the memory block aspect's state
	//TICKLITE_CoreReset on the impl aspect
	//TICKLITE_CheckForExpiration on the impl aspect
	//TICKLITE_OnExpiration
	class TLEntityFinalTickResolver : public UArtilleryDispatch::TL_ThreadedImpl /*Facaded*/
	{
	public:
		FSkeletonKey EntityKey;
		TLEntityFinalTickResolver()
		{
		}

		TLEntityFinalTickResolver(
			FSkeletonKey Target
			) : TL_ThreadedImpl(), EntityKey(Target)
		{
		}
		void TICKLITE_StateReset()
		{
		}
		void TICKLITE_Calculate()
		{
		}


		void RechargeClamp(AttrPtr bindH, AttribKey Max, AttribKey Current)
		{
			if(bindH != nullptr && bindH->GetCurrentValue() > 0)
			{
				auto bindHMax = ADispatch->GetAttrib(EntityKey, Max);
				auto bindHCur = ADispatch->GetAttrib(EntityKey, Current);
				if(
					(bindHMax != nullptr && bindHMax->GetCurrentValue() > 0) &&
					(bindHCur != nullptr)) //note that current does not check 0. lmao. it used to.
				{
					auto clamped = std::min(bindH->GetCurrentValue() + bindHCur->GetCurrentValue(), bindHMax->GetCurrentValue());
					bindHCur->SetCurrentValue(clamped);
				}
			}
		}

		//This can be set up to autowire, but I'm not sure we're keeping these mechanisms yet.
		//we can speed this up considerably by adding a get all attribs. not sure we wanna though until optimization demands it.
		void TICKLITE_Apply()
		{
			//factor the get attr down to the impl.
			bool ManaR = ADispatch->GetAttribAndApplyIf(EntityKey,  Attr::ManaRechargePerTick,
			[this](AttrPtr At){this->RechargeClamp(At, Attr::MaxMana, Attr::Mana); return true;});
			bool ShieldsR = ADispatch->GetAttribAndApplyIf(EntityKey,  Attr::ShieldsRechargePerTick,
			[this](AttrPtr At){this->RechargeClamp(At, Attr::MaxShields, Attr::Shields); return true;});
			bool HealthR = ADispatch->GetAttribAndApplyIf(EntityKey,  Attr::HealthRechargePerTick,
			[this](AttrPtr At){this->RechargeClamp(At, Attr::MaxHealth, Attr::Health); return true;});

			

			bool proposed = ADispatch->GetAttribAndApplyIf(EntityKey,  Attr::ProposedDamage,
			[this](AttrPtr ProposedDamage){
				auto RemainingDamageToApply = ProposedDamage->GetCurrentValue();

				bool ShieldsFound = ADispatch->GetAttribAndApplyIf(EntityKey,  Attr::Shields,
				[this, &RemainingDamageToApply](AttrPtr Shields){
					auto CurrentShieldsValue = Shields->GetCurrentValue();
					auto NewShieldsValue = std::max(0.f, CurrentShieldsValue - RemainingDamageToApply);
					Shields->SetCurrentValue(NewShieldsValue);
					RemainingDamageToApply = std::max(0.f, RemainingDamageToApply - CurrentShieldsValue);
					return true;
				});
				
				bool HealthFound = ADispatch->GetAttribAndApplyIf(EntityKey,  Attr::Health,
				[this, &RemainingDamageToApply](AttrPtr Health){
					auto NewHealthValue = std::max(0.f, Health->GetCurrentValue() - RemainingDamageToApply);
					Health->SetCurrentValue(NewHealthValue);
					return true;
				});
				// Finally, reset ProposedDamage back to zero since we've applied it all.
				ProposedDamage->SetCurrentValue(0.f);
				return HealthFound || ShieldsFound;
			});
				

			}
		
		void TICKLITE_CoreReset()
		{
		}

		bool TICKLITE_CheckForExpiration()
		{
			return false; //add check for aliveness of ya owner, factor that down.
		}

		void TICKLITE_OnExpiration()
		{
			//no op
		}
	};

typedef Ticklites::Ticklite<TLEntityFinalTickResolver> EntityFinalTickResolver;

