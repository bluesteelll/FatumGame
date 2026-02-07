// Shared inline helpers for Flecs library classes.

#pragma once

#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"

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
}
