// Factory method for FResourcePools — converts UObject Data Asset to sim-safe POD.
// Game thread only (accesses UObjects).

#include "FlecsResourceTypes.h"
#include "FlecsResourcePoolProfile.h"

FResourcePools FResourcePools::FromProfile(const UFlecsResourcePoolProfile* Profile)
{
	FResourcePools Result;
	if (!Profile) return Result;

	checkf(Profile->Pools.Num() <= MAX_RESOURCE_POOLS,
		TEXT("ResourcePoolProfile has %d pools but MAX_RESOURCE_POOLS is %d"),
		Profile->Pools.Num(), MAX_RESOURCE_POOLS);

	int32 PoolCount = FMath::Min(Profile->Pools.Num(), static_cast<int32>(MAX_RESOURCE_POOLS));
	for (int32 p = 0; p < PoolCount; ++p)
	{
		const FResourcePoolDefinition& PoolDef = Profile->Pools[p];
		if (PoolDef.ResourceType == EResourceType::None) continue;

		FResourcePool& Pool = Result.Pools[Result.PoolCount];
		Pool.TypeId = static_cast<EResourceTypeId>(PoolDef.ResourceType);
		Pool.MaxValue = PoolDef.MaxValue;
		Pool.CurrentValue = PoolDef.GetStartingValue();
		Pool.BaseRegenRate = PoolDef.RegenRate;
		Pool.RegenDelay = PoolDef.RegenDelay;
		Pool.RegenDelayTimer = 0.f;
		Pool.RegenAccumulator = 0.f;
		Pool.bRegenWhileChanneling = PoolDef.bRegenWhileChanneling;
		Result.PoolCount++;
	}

	return Result;
}
