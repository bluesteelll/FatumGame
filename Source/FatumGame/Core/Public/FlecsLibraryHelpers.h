// Shared inline helpers for Flecs library classes.

#pragma once

#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "FlecsBarrageComponents.h"

namespace FlecsLibrary
{
	inline UFlecsArtillerySubsystem* GetSubsystem(UObject* WorldContextObject)
	{
		if (!WorldContextObject) return nullptr;
		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
		if (!World) return nullptr;
		return World->GetSubsystem<UFlecsArtillerySubsystem>();
	}

	inline flecs::entity GetEntityForKey(UFlecsArtillerySubsystem* Sub, FSkeletonKey Key)
	{
		if (!Sub || !Key.IsValid()) return flecs::entity();
		return Sub->GetEntityForBarrageKey(Key);
	}

	inline UBarrageDispatch* GetBarrageDispatch(UObject* WorldContextObject)
	{
		if (!WorldContextObject) return nullptr;
		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
		if (!World) return nullptr;
		return World->GetSubsystem<UBarrageDispatch>();
	}

	/** Register a constraint on a Flecs entity. Called inside EnqueueCommand (sim thread, non-deferred).
	 *  Uses obtain<>() -- creates FFlecsConstraintData if absent, returns ref if present. */
	inline void RegisterConstraintOnEntity(
		flecs::entity E, int64 ConstraintKey, FSkeletonKey OtherKey,
		float BreakForce, float BreakTorque)
	{
		if (!E.is_valid() || !E.is_alive()) return;
		FFlecsConstraintData& Data = E.obtain<FFlecsConstraintData>();
		Data.AddConstraint(ConstraintKey, OtherKey, BreakForce, BreakTorque);
		E.add<FTagConstrained>();
	}
}
