// Resource cost check/commit, sustain drain, and regeneration implementations.
// Separate from header due to circular dependency (FAbilitySlot ↔ FAbilityCostEntry).

#include "FlecsAbilityTypes.h" // includes FlecsResourceTypes.h

bool CheckActivationCosts(const FResourcePools& Pools, const FAbilitySlot& Slot)
{
	for (int32 i = 0; i < Slot.ActivationCostCount; ++i)
	{
		const FAbilityCostEntry& Cost = Slot.ActivationCosts[i];
		if (!Cost.IsValid()) continue;
		int32 PoolIdx = Pools.FindPool(Cost.ResourceType);
		checkf(PoolIdx != INDEX_NONE,
			TEXT("CheckActivationCosts: ability requires resource %d but entity has no such pool"),
			static_cast<int32>(Cost.ResourceType));
		if (Pools.Pools[PoolIdx].CurrentValue < Cost.Amount) return false;
	}
	return true;
}

void CommitActivationCosts(FResourcePools& Pools, const FAbilitySlot& Slot)
{
	for (int32 i = 0; i < Slot.ActivationCostCount; ++i)
	{
		const FAbilityCostEntry& Cost = Slot.ActivationCosts[i];
		if (!Cost.IsValid()) continue;
		Pools.Consume(Cost.ResourceType, Cost.Amount);
	}
}

bool ApplySustainDrain(FResourcePools& Pools, const FAbilitySlot& Slot, float DeltaTime)
{
	for (int32 i = 0; i < Slot.SustainCostCount; ++i)
	{
		const FAbilityCostEntry& Cost = Slot.SustainCosts[i];
		if (!Cost.IsValid()) continue;

		float DrainThisTick = Cost.Amount * DeltaTime;
		int32 PoolIdx = Pools.FindPool(Cost.ResourceType);
		checkf(PoolIdx != INDEX_NONE,
			TEXT("ApplySustainDrain: resource %d not found"),
			static_cast<int32>(Cost.ResourceType));

		FResourcePool& Pool = Pools.Pools[PoolIdx];
		Pool.CurrentValue -= DrainThisTick;
		Pool.RegenDelayTimer = Pool.RegenDelay;

		if (Pool.CurrentValue <= 0.f)
		{
			Pool.CurrentValue = 0.f;
			return false; // depleted
		}
	}
	return true;
}

void TickResourceRegen(FResourcePools& Pools, float DeltaTime, bool bAnyChanneledActive)
{
	for (int32 i = 0; i < Pools.PoolCount; ++i)
	{
		FResourcePool& Pool = Pools.Pools[i];
		if (Pool.BaseRegenRate <= 0.f) continue;
		if (Pool.CurrentValue >= Pool.MaxValue) continue;

		// Suppress regen while channeling if configured
		if (bAnyChanneledActive && !Pool.bRegenWhileChanneling) continue;

		// Tick regen delay
		if (Pool.RegenDelayTimer > 0.f)
		{
			Pool.RegenDelayTimer = FMath::Max(0.f, Pool.RegenDelayTimer - DeltaTime);
			continue;
		}

		// Accumulator pattern (same as FHealthInstance::RegenAccumulator)
		Pool.RegenAccumulator += Pool.BaseRegenRate * DeltaTime;
		if (Pool.RegenAccumulator >= 1.f)
		{
			float WholeUnits = FMath::FloorToFloat(Pool.RegenAccumulator);
			Pool.CurrentValue = FMath::Min(Pool.CurrentValue + WholeUnits, Pool.MaxValue);
			Pool.RegenAccumulator -= WholeUnits;
			// Clear accumulator when pool is full (no regen "bonus" on next consumption)
			if (Pool.CurrentValue >= Pool.MaxValue)
				Pool.RegenAccumulator = 0.f;
		}
	}
}
