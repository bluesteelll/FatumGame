// Unified Character Action State System
// Bitmask-based concurrent state with declarative rule table.
// Header-only: all constexpr/inline, zero runtime overhead.

#pragma once

#include <cstdint>
#include "FlecsMovementComponents.h"  // ECharacterMoveMode, ECharacterPosture

// ═══════════════════════════════════════════════════════════════
// ACTION STATE BITS
// Single uint64 bitmask. Groups are logically partitioned by bit range.
// Exclusive groups: only one bit active within group.
// Concurrent groups: multiple bits can be active simultaneously.
// ═══════════════════════════════════════════════════════════════

namespace ActionBit
{
	// Locomotion (bits 0-7, EXCLUSIVE)
	// No bit set = Idle
	constexpr uint64 Walking     = 1ULL << 0;
	constexpr uint64 Sprinting   = 1ULL << 1;
	constexpr uint64 Jumping     = 1ULL << 2;
	constexpr uint64 Falling     = 1ULL << 3;
	constexpr uint64 Sliding     = 1ULL << 4;
	constexpr uint64 Mantling    = 1ULL << 5;
	constexpr uint64 Climbing    = 1ULL << 6;
	constexpr uint64 LedgeHang   = 1ULL << 7;

	// Posture (bits 8-10, EXCLUSIVE)
	// No bit set = Standing
	constexpr uint64 Crouching   = 1ULL << 8;
	constexpr uint64 Prone       = 1ULL << 9;

	// Combat (bits 16-23, CONCURRENT)
	constexpr uint64 Firing      = 1ULL << 16;
	constexpr uint64 Reloading   = 1ULL << 17;
	constexpr uint64 ADS         = 1ULL << 18;
	constexpr uint64 WeaponRetracted = 1ULL << 19;
	constexpr uint64 WeaponSwitching = 1ULL << 20;

	// Abilities (bits 24-31, CONCURRENT)
	constexpr uint64 BlinkAiming      = 1ULL << 24;
	constexpr uint64 TelekinesisActive = 1ULL << 25;

	// Interaction (bits 32-39, EXCLUSIVE)
	constexpr uint64 InteractFocus  = 1ULL << 32;
	constexpr uint64 InteractHold   = 1ULL << 33;
	constexpr uint64 InventoryOpen  = 1ULL << 34;
	constexpr uint64 LootPanelOpen  = 1ULL << 35;

	// Status (bits 40-47, CONCURRENT)
	constexpr uint64 Dead        = 1ULL << 40;
	constexpr uint64 Stunned     = 1ULL << 41;
}

// ═══════════════════════════════════════════════════════════════
// INPUT STATE BITS (raw physical input, always tracks key state)
// ═══════════════════════════════════════════════════════════════

namespace InputBit
{
	constexpr uint64 FireHeld    = 1ULL << 0;
	constexpr uint64 SprintHeld  = 1ULL << 1;
	constexpr uint64 CrouchHeld  = 1ULL << 2;
	constexpr uint64 ADSHeld     = 1ULL << 3;
	constexpr uint64 JumpHeld    = 1ULL << 4;
	constexpr uint64 InteractHeld = 1ULL << 5;
	constexpr uint64 BlinkHeld   = 1ULL << 6;
	constexpr uint64 ProneHeld   = 1ULL << 7;
}

// ═══════════════════════════════════════════════════════════════
// GROUP MASKS (for exclusive group clearing)
// ═══════════════════════════════════════════════════════════════

constexpr uint64 LOCOMOTION_MASK   = 0x0000'0000'0000'00FF; // bits 0-7
constexpr uint64 POSTURE_MASK      = 0x0000'0000'0000'0700; // bits 8-10
constexpr uint64 COMBAT_MASK       = 0x0000'0000'00FF'0000; // bits 16-23
constexpr uint64 ABILITY_MASK      = 0x0000'0000'FF00'0000; // bits 24-31
constexpr uint64 INTERACTION_MASK  = 0x0000'00FF'0000'0000; // bits 32-39
constexpr uint64 STATUS_MASK       = 0x0000'FF00'0000'0000; // bits 40-47

// Shorthand: all states that represent "character is busy / not in normal gameplay"
constexpr uint64 BUSY_MASK = ActionBit::Mantling | ActionBit::Climbing | ActionBit::LedgeHang
	| ActionBit::Dead | ActionBit::Stunned;

// ═══════════════════════════════════════════════════════════════
// ACTION RULE TABLE
// Per-action: BlockedByStates, CanceledOnEntry, ExclusiveGroupMask
// RequiredStates omitted (none needed currently — all actions check blocked only)
// ═══════════════════════════════════════════════════════════════

struct FActionRule
{
	uint64 StateBit;           // Which bit this rule governs
	uint64 BlockedByStates;    // ANY of these active → entry denied
	uint64 CanceledOnEntry;    // These bits are CLEARED when this state activates
	uint64 ExclusiveGroupMask; // Clear group before setting (0 = concurrent, no group)
};

// Rule table — constexpr array, looked up by StateBit match
inline constexpr FActionRule GActionRules[] =
{
	// Locomotion (exclusive group)
	{ ActionBit::Walking,   ActionBit::Dead | ActionBit::Stunned,
		0, LOCOMOTION_MASK },

	{ ActionBit::Sprinting, ActionBit::Firing | ActionBit::Reloading | ActionBit::ADS
		| ActionBit::Sliding | ActionBit::Mantling | ActionBit::Climbing | ActionBit::LedgeHang
		| ActionBit::Dead | ActionBit::Stunned | ActionBit::InventoryOpen | ActionBit::LootPanelOpen
		| ActionBit::Prone | ActionBit::Crouching,
		0, LOCOMOTION_MASK },

	{ ActionBit::Jumping,   ActionBit::Dead | ActionBit::Stunned | ActionBit::Mantling | ActionBit::Climbing,
		0, LOCOMOTION_MASK },

	{ ActionBit::Falling,   0, 0, LOCOMOTION_MASK },
	{ ActionBit::Sliding,   ActionBit::Dead | ActionBit::Stunned,
		ActionBit::Sprinting | ActionBit::ADS, LOCOMOTION_MASK },
	{ ActionBit::Mantling,  ActionBit::Dead | ActionBit::Stunned,
		ActionBit::Firing | ActionBit::ADS | ActionBit::Sprinting | ActionBit::Reloading, LOCOMOTION_MASK },
	{ ActionBit::Climbing,  ActionBit::Dead | ActionBit::Stunned,
		ActionBit::Firing | ActionBit::ADS | ActionBit::Sprinting | ActionBit::Reloading, LOCOMOTION_MASK },
	{ ActionBit::LedgeHang, ActionBit::Dead | ActionBit::Stunned,
		ActionBit::Firing | ActionBit::ADS | ActionBit::Sprinting | ActionBit::Reloading, LOCOMOTION_MASK },

	// Posture (exclusive group)
	{ ActionBit::Crouching, ActionBit::Dead | ActionBit::Stunned | ActionBit::Sprinting | ActionBit::Sliding,
		ActionBit::Prone, POSTURE_MASK },
	{ ActionBit::Prone,     ActionBit::Dead | ActionBit::Stunned | ActionBit::Sprinting | ActionBit::Sliding,
		ActionBit::Crouching, POSTURE_MASK },

	// Combat (concurrent — no exclusive group)
	{ ActionBit::Firing,    ActionBit::Mantling | ActionBit::Climbing | ActionBit::LedgeHang
		| ActionBit::Dead | ActionBit::Stunned
		| ActionBit::InventoryOpen | ActionBit::LootPanelOpen | ActionBit::Reloading | ActionBit::WeaponSwitching,
		ActionBit::Sprinting, 0 },

	{ ActionBit::Reloading, ActionBit::Mantling | ActionBit::Climbing | ActionBit::LedgeHang
		| ActionBit::Dead | ActionBit::Stunned | ActionBit::InventoryOpen | ActionBit::LootPanelOpen
		| ActionBit::WeaponSwitching,
		ActionBit::Sprinting | ActionBit::Firing, 0 },

	{ ActionBit::ADS,       ActionBit::Mantling | ActionBit::Climbing | ActionBit::LedgeHang
		| ActionBit::Dead | ActionBit::Stunned
		| ActionBit::InventoryOpen | ActionBit::LootPanelOpen | ActionBit::WeaponSwitching,
		ActionBit::Sprinting, 0 },

	{ ActionBit::WeaponSwitching, ActionBit::Mantling | ActionBit::Climbing | ActionBit::LedgeHang
		| ActionBit::Dead | ActionBit::Stunned
		| ActionBit::InventoryOpen | ActionBit::LootPanelOpen,
		ActionBit::Firing | ActionBit::Reloading | ActionBit::ADS | ActionBit::Sprinting, 0 },

	// Interaction (exclusive group)
	{ ActionBit::InteractFocus, ActionBit::Dead,
		ActionBit::Firing | ActionBit::ADS | ActionBit::Sprinting, INTERACTION_MASK },
	{ ActionBit::InteractHold,  ActionBit::Dead,
		ActionBit::Firing | ActionBit::ADS | ActionBit::Sprinting, INTERACTION_MASK },
	{ ActionBit::InventoryOpen, ActionBit::Dead,
		ActionBit::Sprinting | ActionBit::Firing | ActionBit::ADS | ActionBit::Reloading, INTERACTION_MASK },
	{ ActionBit::LootPanelOpen, ActionBit::Dead,
		ActionBit::Sprinting | ActionBit::Firing | ActionBit::ADS | ActionBit::Reloading, INTERACTION_MASK },

	// Status
	{ ActionBit::Dead,    0, 0xFFFF'FFFF'FFFF'FFFF, 0 }, // Cancels everything
	{ ActionBit::Stunned, 0, ActionBit::Firing | ActionBit::Reloading | ActionBit::ADS | ActionBit::Sprinting, 0 },
};

inline constexpr int32 GActionRuleCount = sizeof(GActionRules) / sizeof(GActionRules[0]);

// ═══════════════════════════════════════════════════════════════
// DEFERRED TRANSITIONS
// When OnExitOfState is cleared AND InputRequired is held → TryEnter TryEnterState
// ═══════════════════════════════════════════════════════════════

struct FDeferredTransition
{
	uint64 OnExitOfState;  // Trigger when this bit is cleared
	uint64 InputRequired;  // This input bit must be held
	uint64 TryEnterState;  // Attempt to set this action bit
};

inline constexpr FDeferredTransition GDeferredTransitions[] =
{
	{ ActionBit::Firing,          InputBit::SprintHeld,  ActionBit::Sprinting },
	{ ActionBit::Reloading,       InputBit::SprintHeld,  ActionBit::Sprinting },
	{ ActionBit::ADS,             InputBit::SprintHeld,  ActionBit::Sprinting },
	{ ActionBit::WeaponSwitching, InputBit::SprintHeld,  ActionBit::Sprinting },
	// Slide→Crouch deferred transition handled by sim thread posture management (not game thread)
};

inline constexpr int32 GDeferredTransitionCount = sizeof(GDeferredTransitions) / sizeof(GDeferredTransitions[0]);

// ═══════════════════════════════════════════════════════════════
// INLINE HELPERS (O(1) bitwise operations)
// ═══════════════════════════════════════════════════════════════

inline bool HasBit(uint64 State, uint64 Bit)    { return (State & Bit) != 0; }
inline bool HasAny(uint64 State, uint64 Mask)   { return (State & Mask) != 0; }
inline bool HasAll(uint64 State, uint64 Mask)   { return (State & Mask) == Mask; }

// Find rule for a given state bit. Returns nullptr if no rule defined.
inline const FActionRule* FindActionRule(uint64 Bit)
{
	for (int32 i = 0; i < GActionRuleCount; ++i)
	{
		if (GActionRules[i].StateBit == Bit)
			return &GActionRules[i];
	}
	return nullptr;
}

// Check if state can be entered given current full state.
inline bool CanEnterState(uint64 FullState, uint64 Bit)
{
	const FActionRule* Rule = FindActionRule(Bit);
	if (!Rule) return true; // No rule = always allowed
	return (FullState & Rule->BlockedByStates) == 0;
}

// Attempt state entry. Returns new state and success flag.
struct FStateTransitionResult
{
	uint64 NewState;
	uint64 CanceledBits; // Bits that were force-cleared
	bool bSuccess;
};

inline FStateTransitionResult TryEnterState(uint64 CurrentState, uint64 Bit)
{
	const FActionRule* Rule = FindActionRule(Bit);

	// No rule → unconditionally set
	if (!Rule)
		return { CurrentState | Bit, 0, true };

	// Check blocked
	if ((CurrentState & Rule->BlockedByStates) != 0)
		return { CurrentState, 0, false };

	uint64 NewState = CurrentState;

	// Cancel conflicting states
	uint64 Canceled = NewState & Rule->CanceledOnEntry;
	NewState &= ~Rule->CanceledOnEntry;

	// Clear exclusive group
	if (Rule->ExclusiveGroupMask != 0)
		NewState &= ~Rule->ExclusiveGroupMask;

	// Set the new bit
	NewState |= Bit;

	return { NewState, Canceled, true };
}

// Exit a state and process deferred transitions.
inline uint64 ProcessDeferredTransitions(uint64 StateAfterExit, uint64 ExitedBit, uint64 InputState)
{
	for (int32 i = 0; i < GDeferredTransitionCount; ++i)
	{
		const auto& Def = GDeferredTransitions[i];
		if (Def.OnExitOfState != ExitedBit) continue;
		if ((InputState & Def.InputRequired) == 0) continue;

		// Try to enter the deferred state
		auto Result = TryEnterState(StateAfterExit, Def.TryEnterState);
		if (Result.bSuccess)
			StateAfterExit = Result.NewState;
	}
	return StateAfterExit;
}

// ═══════════════════════════════════════════════════════════════
// BACKWARD COMPAT: extract ECharacterMoveMode / ECharacterPosture from bitmask
// ═══════════════════════════════════════════════════════════════

inline ECharacterMoveMode GetMoveModeFromState(uint64 State)
{
	uint64 Loco = State & LOCOMOTION_MASK;
	if (Loco == 0)                         return ECharacterMoveMode::Idle;
	if (Loco & ActionBit::Walking)         return ECharacterMoveMode::Walk;
	if (Loco & ActionBit::Sprinting)       return ECharacterMoveMode::Sprint;
	if (Loco & ActionBit::Jumping)         return ECharacterMoveMode::Jump;
	if (Loco & ActionBit::Falling)         return ECharacterMoveMode::Fall;
	if (Loco & ActionBit::Sliding)         return ECharacterMoveMode::Slide;
	if (Loco & ActionBit::Mantling)        return ECharacterMoveMode::Mantle;
	if (Loco & ActionBit::Climbing)        return ECharacterMoveMode::Mantle; // closest match
	if (Loco & ActionBit::LedgeHang)       return ECharacterMoveMode::LedgeHang;
	return ECharacterMoveMode::Idle;
}

inline ECharacterPosture GetPostureFromState(uint64 State)
{
	if (State & ActionBit::Crouching) return ECharacterPosture::Crouching;
	if (State & ActionBit::Prone)     return ECharacterPosture::Prone;
	return ECharacterPosture::Standing;
}
