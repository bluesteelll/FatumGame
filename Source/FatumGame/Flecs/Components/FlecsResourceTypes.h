// Ability resource pool types — Mana, Stamina, Energy, etc.
// ECS component FResourcePools holds up to 4 typed pools per character entity.
// Free functions for cost check/commit, sustain drain, and regen.
// Sim-thread only (mutations). Game thread reads via FSimStateCache.

#pragma once
#include "CoreMinimal.h"

// Resource type identifiers. Extend as needed — values must match EResourceType (Blueprint mirror).
enum class EResourceTypeId : uint8
{
	None = 0,
	Mana = 1,
	Stamina = 2,
	Energy = 3,
	Rage = 4,
	MAX
};

static constexpr int32 MAX_RESOURCE_POOLS = 4;
static constexpr int32 MAX_ABILITY_COSTS = 2;

// ─── Single resource pool (mutable per-entity) ───

struct FResourcePool
{
	EResourceTypeId TypeId = EResourceTypeId::None;
	float MaxValue = 100.f;
	float CurrentValue = 100.f;
	float BaseRegenRate = 0.f;       // units/sec (0 = no passive regen)
	float RegenDelay = 0.f;          // seconds after consumption before regen starts
	float RegenDelayTimer = 0.f;     // remaining delay (counts down)
	float RegenAccumulator = 0.f;    // fractional regen accumulator
	bool bRegenWhileChanneling = true;

	bool IsEmpty() const { return TypeId == EResourceTypeId::None; }
	bool IsDepleted() const { return CurrentValue <= 0.f; }
	float GetRatio() const { return MaxValue > 0.f ? FMath::Clamp(CurrentValue / MaxValue, 0.f, 1.f) : 0.f; }
};

// ─── Per-character resource state (all pools in one ECS component) ───

struct FResourcePools
{
	FResourcePool Pools[MAX_RESOURCE_POOLS];
	uint8 PoolCount = 0;

	int32 FindPool(EResourceTypeId Type) const
	{
		for (int32 i = 0; i < PoolCount; ++i)
			if (Pools[i].TypeId == Type) return i;
		return INDEX_NONE;
	}

	bool CanAfford(EResourceTypeId Type, float Amount) const
	{
		int32 Idx = FindPool(Type);
		checkf(Idx != INDEX_NONE, TEXT("FResourcePools::CanAfford: pool %d not found"), static_cast<int32>(Type));
		return Pools[Idx].CurrentValue >= Amount;
	}

	void Consume(EResourceTypeId Type, float Amount)
	{
		int32 Idx = FindPool(Type);
		checkf(Idx != INDEX_NONE, TEXT("FResourcePools::Consume: pool %d not found"), static_cast<int32>(Type));
		Pools[Idx].CurrentValue = FMath::Max(0.f, Pools[Idx].CurrentValue - Amount);
		Pools[Idx].RegenDelayTimer = Pools[Idx].RegenDelay;
	}

	void Refund(EResourceTypeId Type, float Amount)
	{
		int32 Idx = FindPool(Type);
		checkf(Idx != INDEX_NONE, TEXT("FResourcePools::Refund: pool %d not found"), static_cast<int32>(Type));
		Pools[Idx].CurrentValue = FMath::Min(Pools[Idx].CurrentValue + Amount, Pools[Idx].MaxValue);
	}
};

// ─── Cost entry (which resource type + how much) ───

struct FAbilityCostEntry
{
	EResourceTypeId ResourceType = EResourceTypeId::None;
	float Amount = 0.f;

	bool IsValid() const { return ResourceType != EResourceTypeId::None && Amount > 0.f; }
};

// ─── Forward declaration ───
struct FAbilitySlot;

// ─── Free functions (used by PrepareCharacterStep + AbilityLifecycleManager) ───
// Implementations in FlecsResourceFunctions.cpp (need full FAbilitySlot definition).

// Check if ALL activation costs are affordable. Returns false if ANY is unaffordable.
bool CheckActivationCosts(const FResourcePools& Pools, const FAbilitySlot& Slot);

// Commit ALL activation costs. Call ONLY after CheckActivationCosts returns true.
void CommitActivationCosts(FResourcePools& Pools, const FAbilitySlot& Slot);

// Apply per-tick sustain drain. Returns false if any pool depleted (ability should end).
bool ApplySustainDrain(FResourcePools& Pools, const FAbilitySlot& Slot, float DeltaTime);

// Tick regeneration for all pools.
void TickResourceRegen(FResourcePools& Pools, float DeltaTime, bool bAnyChanneledActive);
