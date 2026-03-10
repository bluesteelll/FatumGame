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

	// Telekinesis
	FAtomicOneShot TelekinesisToggle; // one-shot: grab/release toggle
	FAtomicOneShot TelekinesisThrow;  // one-shot: throw held object
	FAtomicAxis    TelekinesisScroll; // scroll delta for hold distance adjustment
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
	FAtomicState                TelekinesisActive; // sim→game: holding an object
	FAtomicState                ClimbActive;       // sim→game: climbing a ladder
	FAtomicState                RopeSwingActive;   // sim→game: swinging on a rope
};

// ═══════════════════════════════════════════════════════════════════════════
// GROUPED STATE STRUCTS
// Reduce header bloat by grouping related private members into plain structs.
// ═══════════════════════════════════════════════════════════════════════════

class USkeletalMesh;

/** Pending weapon equip data (sim thread → game thread via atomics).
 *  Processed in Tick() to avoid modifying components during post-tick update phase. */
struct FPendingWeaponEquip
{
	std::atomic<int64> WeaponId{0};
	std::atomic<bool> bPending{false};
	USkeletalMesh* Mesh = nullptr;       // Set on game thread before EnqueueCommand
	FTransform AttachOffset;              // Set on game thread before EnqueueCommand
};

/** Character position interpolation state (game thread only, updated in Tick).
 *  Same pattern as ISM FEntityTransformState + VInterpTo smoothing. */
struct FCharacterPositionState
{
	FVector PrevPos = FVector::ZeroVector;
	FVector CurrPos = FVector::ZeroVector;
	FVector SmoothedPos = FVector::ZeroVector;
	uint64 LastSimTick = 0;
	bool bJustSpawned = true;

	// Feet-to-actor Z offset: CapsuleHalfHeight on ground, frozen in air.
	float FeetToActorOffset = 0.f;

	void SnapTo(const FVector& Pos)
	{
		PrevPos = Pos;
		CurrPos = Pos;
		SmoothedPos = Pos;
		bJustSpawned = false;
	}
};
