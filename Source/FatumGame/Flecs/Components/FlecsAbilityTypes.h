// Generic ability system — core types.
// FAbilitySlot = per-slot state, FAbilitySystem = per-character ability manager.
// Sim-thread only (mutations). Game thread reads via atomics.

#pragma once
#include "CoreMinimal.h"

// Unique ID per ability type. Index into dispatch table.
enum class EAbilityTypeId : uint8
{
	None = 0,
	Slide = 1,
	Blink = 2,
	Mantle = 3,
	KineticBlast = 4,
	MAX
};

// Tick function return value
enum class EAbilityTickResult : uint8
{
	Continue,        // ability continues running
	End,             // ability finished, deactivate slot
	EndAndConsume    // deactivate + prevent re-activation this tick
};

// Max bytes for per-ability-type config stored in FAbilitySlot::ConfigData
static constexpr int32 ABILITY_CONFIG_SIZE = 32;

// State of one ability slot
struct FAbilitySlot
{
	EAbilityTypeId TypeId = EAbilityTypeId::None;
	uint8 Phase = 0;           // 0 = inactive, 1+ = active phase
	float PhaseTimer = 0.f;    // time in current phase (seconds)
	int8 Charges = -1;         // -1 = infinite, 0+ = current charges
	int8 MaxCharges = 1;
	float RechargeTimer = 0.f; // recharge accumulator
	float RechargeRate = 0.f;  // seconds per charge (0 = no recharge)
	float CooldownDuration = 0.f; // per-use cooldown (seconds, 0 = no cooldown)
	float CooldownTimer = 0.f;    // remaining cooldown (ticks down after deactivation)
	bool bAlwaysTick = false;  // tick even when Phase==0 (e.g. Blink: always listening for input)
	bool bExclusive = false;   // when active, only this ability ticks (e.g. Mantle: overrides everything)
	alignas(8) uint8 ConfigData[ABILITY_CONFIG_SIZE] = {}; // per-ability-type config, cast by TypeId

	bool IsActive() const { return Phase > 0; }
	bool IsEmpty() const { return TypeId == EAbilityTypeId::None; }
};

static constexpr int32 MAX_ABILITY_SLOTS = 8;

// One ECS component per character. Always present, never add/remove.
struct FAbilitySystem
{
	uint8 ActiveMask = 0;    // bit N = Slots[N] active
	uint8 SlotCount = 0;     // number of filled slots
	FAbilitySlot Slots[MAX_ABILITY_SLOTS];

	bool IsAnyActive() const { return ActiveMask != 0; }
	bool IsSlotActive(int32 Idx) const { return (ActiveMask & (1 << Idx)) != 0; }

	int32 FindSlotByType(EAbilityTypeId Type) const
	{
		for (int32 i = 0; i < SlotCount; ++i)
			if (Slots[i].TypeId == Type) return i;
		return INDEX_NONE;
	}

	bool IsAbilityActive(EAbilityTypeId Type) const
	{
		int32 Idx = FindSlotByType(Type);
		return Idx != INDEX_NONE && IsSlotActive(Idx);
	}

	// Mutations (sim thread only)
	void ActivateSlot(int32 Idx)
	{
		checkf(Idx >= 0 && Idx < MAX_ABILITY_SLOTS, TEXT("ActivateSlot: Idx %d out of range"), Idx);
		Slots[Idx].Phase = 1;
		Slots[Idx].PhaseTimer = 0.f;
		ActiveMask |= (1 << Idx);
	}

	void DeactivateSlot(int32 Idx)
	{
		checkf(Idx >= 0 && Idx < MAX_ABILITY_SLOTS, TEXT("DeactivateSlot: Idx %d out of range"), Idx);
		Slots[Idx].Phase = 0;
		Slots[Idx].PhaseTimer = 0.f;
		Slots[Idx].CooldownTimer = Slots[Idx].CooldownDuration;
		ActiveMask &= ~(1 << Idx);
	}
};
