// Shared types for character ↔ simulation thread communication.
// Kept in a separate header to avoid pulling FlecsCharacter.h into subsystem code.

#pragma once

#include <atomic>

// ═══════════════════════════════════════════════════════════════════════════
// ATOMIC WRAPPER TYPES
// Encode read/write semantics into the type. Zero overhead (same relaxed atomics).
// ═══════════════════════════════════════════════════════════════════════════

/** Latest-wins atomic. Producer calls Write(), consumer calls Read().
 *  Used for continuous values (movement axes, camera) and held buttons (crouch, sprint). */
template<typename T>
struct TAtomicLatestWins
{
	void Write(T v) { Value.store(v, std::memory_order_relaxed); }
	T Read() const { return Value.load(std::memory_order_relaxed); }
private:
	std::atomic<T> Value{};
};

using FAtomicAxis  = TAtomicLatestWins<float>;  // Continuous float (DirX, CamLocX...)
using FAtomicHeld  = TAtomicLatestWins<bool>;   // Held button (game→sim)
using FAtomicState = TAtomicLatestWins<bool>;   // State flag (sim→game)

/** Fire-and-consume atomic. Producer calls Fire(), consumer calls Consume().
 *  Consume() uses atomic exchange — reads AND clears in one operation.
 *  Used for one-shot events (JumpPressed, Teleported). */
struct FAtomicOneShot
{
	void Fire() { Value.store(true, std::memory_order_relaxed); }
	bool Consume() const { return Value.exchange(false, std::memory_order_relaxed); }
private:
	mutable std::atomic<bool> Value{false};
};

// ═══════════════════════════════════════════════════════════════════════════
// GAME → SIM INPUT
// Written by AFlecsCharacter input handlers. Read by PrepareCharacterStep.
// ═══════════════════════════════════════════════════════════════════════════

struct FCharacterInputAtomics
{
	// Movement direction (world-space, written by Move() every frame)
	FAtomicAxis DirX, DirZ;

	// Camera position/direction (written every Tick for blink targeting + mantle hang exit)
	FAtomicAxis CamLocX, CamLocY, CamLocZ;
	FAtomicAxis CamDirX, CamDirY, CamDirZ;

	// Buttons
	FAtomicOneShot JumpPressed;    // one-shot: sim consumes after processing
	FAtomicHeld    CrouchHeld;     // held: game writes true on press, false on release
	FAtomicHeld    BlinkHeld;      // held: game writes true on press, false on release
	FAtomicHeld    Sprinting;      // held: game writes true on press, false on release
	FAtomicOneShot Ability2Pressed;// one-shot: kinetic blast activation
};

// ═══════════════════════════════════════════════════════════════════════════
// SIM → GAME STATE
// Written by PrepareCharacterStep. Read by AFlecsCharacter::Tick for cosmetics.
// ═══════════════════════════════════════════════════════════════════════════

struct FCharacterStateAtomics
{
	FAtomicState                SlideActive;
	FAtomicState                MantleActive;
	FAtomicState                Hanging;
	FAtomicState                BlinkAiming;
	FAtomicOneShot              Teleported;    // sim fires, game consumes → position snap
	TAtomicLatestWins<uint8>    MantleType;    // 0=Vault, 1=Mantle, 2=LedgeGrab
};
