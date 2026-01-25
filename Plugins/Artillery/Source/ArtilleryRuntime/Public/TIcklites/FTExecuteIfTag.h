#pragma once

#include "Ticklite.h"
#include "ArtilleryDispatch.h"
#include "UArtilleryGameplayTagContainer.h"
#include "FArtilleryTicklitesThread.h"

/**
 * Current this just checks if a given Key has been given the appropriate tags in the Artillery Dispatch
 *
 * This may be used for things like waiting to request a particle system be spawned and we want to attach a particle system
 * to that mesh using the request router as well
 */
class FTExecuteIfTag : public UArtilleryDispatch::TL_ThreadedImpl
{
public:
	std::function<void(const FSkeletonKey&)> FunctionToExecute;
	FSkeletonKey KeyToCheck;
	FGameplayTagContainer RequiredTags;

	// In case we're checking the tags of something that's owned by something else, like a projectile
	FSkeletonKey ParentKey;

	int TicksAliveFor = 0;
	// We may not want to execute something if it has been too long since it was originally requested.
	// If this is not zero and TicksAlive is >=, time out and end the ticklite without executing
	int TickTimeoutCount;
	bool ShouldExecute = false;

	FTExecuteIfTag() : TickTimeoutCount(0)
	{
		FunctionToExecute = nullptr;
		KeyToCheck = FSkeletonKey::Invalid();
		ParentKey = FSkeletonKey::Invalid();
		TickTimeoutCount = 0;
	}

	FTExecuteIfTag(
		const FSkeletonKey ToCheck,
		const FSkeletonKey Parent,
		const std::function<void(const FSkeletonKey&)>& ToExec,
		std::initializer_list<FGameplayTag> Tags,
		int TimeOutAfter)
	{
		FunctionToExecute = ToExec;
		KeyToCheck = ToCheck;
		ParentKey = Parent;

		RequiredTags.CreateFromArray(TArray(Tags));
		TickTimeoutCount = TimeOutAfter;
	}

	FTExecuteIfTag(
		const FSkeletonKey ToCheck,
		const FSkeletonKey Parent,
		std::initializer_list<FGameplayTag> Tags,
		int TimeOutAfter) : FTExecuteIfTag(ToCheck, Parent, nullptr, Tags, TimeOutAfter)
	{
	}
	
	void TICKLITE_StateReset()
	{
	}
	
	void TICKLITE_Calculate()
	{
		FConservedTags TagContainer = this->ADispatch->DispatchOwner->GetExistingConservedTags(KeyToCheck);

		if (TagContainer.IsValid())
		{
			for (auto each : RequiredTags)
			{
				if (TagContainer->Find(each))
				{
					ShouldExecute = true;				
				}
				else
				{
					ShouldExecute = false;
					break;
				}
			}
			
		}
	}
	
	void TICKLITE_Apply()
	{
		if (ShouldExecute)
		{
			FunctionToExecute(KeyToCheck);
			FunctionToExecute = nullptr;
		}
		++TicksAliveFor;
	}
	
	void TICKLITE_CoreReset()
	{
	}

	bool TICKLITE_CheckForExpiration()
	{
		return FunctionToExecute == nullptr || (TickTimeoutCount > 0 && TicksAliveFor >= TickTimeoutCount);
	}

	void TICKLITE_OnExpiration()
	{
	}
};

/**
 * Current this just checks if a given Key has been given the appropriate tags in the Artillery Dispatch
 *
 * This may be used for things like waiting to request a particle system be spawned and we want to attach a particle system
 * to that mesh using the request router as well
 */
class FTExecuteIfFBLIsValid : public UArtilleryDispatch::TL_ThreadedImpl
{
public:
	std::function<void(const FSkeletonKey&)> FunctionToExecute;
	FSkeletonKey KeyToCheck;

	// In case we're checking the tags of something that's owned by something else, like a projectile
	FSkeletonKey ParentKey;

	int TicksAliveFor = 0;
	// We may not want to execute something if it has been too long since it was originally requested.
	// If this is not zero and TicksAlive is >=, time out and end the ticklite without executing
	int TickTimeoutCount;
	bool ShouldExecute = false;

	FTExecuteIfFBLIsValid() : TickTimeoutCount(0)
	{
		FunctionToExecute = nullptr;
		KeyToCheck = FSkeletonKey::Invalid();
		ParentKey = FSkeletonKey::Invalid();
		TickTimeoutCount = 0;
	}

	FTExecuteIfFBLIsValid(
		const FSkeletonKey ToCheck,
		const FSkeletonKey Parent,
		const std::function<void(const FSkeletonKey&)>& ToExec,
		int TimeOutAfter)
	{
		FunctionToExecute = ToExec;
		KeyToCheck = ToCheck;
		ParentKey = Parent;
		TickTimeoutCount = TimeOutAfter;
	}

	FTExecuteIfFBLIsValid(
		const FSkeletonKey ToCheck,
		const FSkeletonKey Parent,
		int TimeOutAfter) : FTExecuteIfFBLIsValid(ToCheck, Parent, nullptr, TimeOutAfter)
	{
	}
	
	void TICKLITE_StateReset()
	{
	}
	
	void TICKLITE_Calculate()
	{
		ArtilleryTime Now = this->GetShadowNow();
		FBLet PhysicsObject = this->ADispatch->GetFBLetByObjectKey(KeyToCheck, Now);
		if(FBarragePrimitive::IsNotNull(PhysicsObject))
		{
			ShouldExecute = true;
		}
	}
	
	void TICKLITE_Apply()
	{

		if (ShouldExecute)
		{
			FunctionToExecute(KeyToCheck);
			FunctionToExecute = nullptr;
		}
		++TicksAliveFor;
	}
	
	void TICKLITE_CoreReset()
	{
	}

	bool TICKLITE_CheckForExpiration()
	{
		return FunctionToExecute == nullptr || (TickTimeoutCount > 0 && TicksAliveFor >= TickTimeoutCount);
	}

	void TICKLITE_OnExpiration()
	{
	}
};

typedef Ticklites::Ticklite<FTExecuteIfTag> TL_ExecuteOnTagExists;
typedef Ticklites::Ticklite<FTExecuteIfFBLIsValid> TL_ExecuteIfFBLIsValid;