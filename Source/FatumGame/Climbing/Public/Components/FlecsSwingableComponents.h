// Swingable entity components (ropes, chains, vines, etc.)
// FSwingableStatic = prefab (immutable params from UFlecsRopeSwingProfile).
// Anchor position computed at activation time from physics body.

#pragma once

#include "FlecsCharacterTypes.h" // FAtomicAxis, FAtomicState
#include <atomic>

// Anchor type for future extension (moving bodies, destructible fragments)
enum class ERopeAnchorType : uint8
{
	Fixed = 0,       // world-fixed point (v1)
	MovingBody = 1,  // attached to a moving Barrage body (v2)
	Fragment = 2,    // attached to a destructible fragment (v2)
};

// PREFAB component: one per swingable type, populated from UFlecsRopeSwingProfile at entity creation.
// Read by TryActivateRopeSwing on sim thread.
struct FSwingableStatic
{
	float MaxRopeLength = 4.f;           // max rope length (Jolt meters)
	float MinGrabLength = 0.5f;          // closest to anchor character can climb (Jolt meters)
	float SwingGravityMultiplier = 1.5f; // amplify gravity for faster arcs
	float SwingInputStrength = 10.f;     // m/s^2 tangential force from input
	float AirDragCoefficient = 0.3f;     // velocity damping per second
	float ClimbDragMultiplier = 3.f;     // additional drag while climbing
	float ClimbSpeedUp = 1.5f;           // rope climb speed upward (Jolt m/s)
	float ClimbSpeedDown = 2.f;          // rope climb speed downward (Jolt m/s)
	float JumpOffBoost = 3.5f;           // vertical boost on jump-off (Jolt m/s)
	float EnterLerpDuration = 0.12f;     // seconds
	float TopDismountDuration = 0.2f;    // seconds
	float SwingClimbThreshold = 0.7f;    // dot product threshold for climb vs swing
	int32 VerletSegments = 8;            // visual chain segments

	// ── Visual params (game thread only) ──
	float VisualDamping = 0.97f;         // Verlet damping (lower = heavier rope feel)
	int32 ConstraintIterations = 3;      // Verlet stiffness (more = stiffer)
	float RopeWidthBase = 4.f;           // cm, thickness at anchor
	float RopeWidthTip = 2.5f;           // cm, thickness at character hand

	// Raw UNiagaraSystem* from Data Asset (DA keeps it GC-rooted).
	// Passed to game thread via FRopeVisualAtomics::NiagaraSystemPtr.
	void* NiagaraSystem = nullptr;
};

// Tag marking an entity as swingable (zero-size)
struct FTagSwingable
{
};

// ═══════════════════════════════════════════════════════════════════════════
// ROPE VISUAL ATOMICS — sim→game bridge for rope rendering
// Owned by AFlecsCharacter, shared with FCharacterPhysBridge via TSharedPtr.
// Sim thread writes after TickRopeSwing, game thread reads in Tick() for Verlet + Niagara.
// ═══════════════════════════════════════════════════════════════════════════

struct FRopeVisualAtomics
{
	FAtomicState bActive;                    // rope swing currently active
	FAtomicAxis  AnchorX, AnchorY, AnchorZ;  // UE coords (cm)
	FAtomicAxis  RopeLength;                  // cm
	FAtomicAxis  VelX, VelY, VelZ;            // UE coords (cm/s)
	TAtomicLatestWins<uint8> Phase;           // FSM phase (0=enter, 1=swing, 3=dismount)
	TAtomicLatestWins<int32> SegmentCount;    // visual verlet segments
	FAtomicAxis  VisualDamping;               // Verlet damping (0.8-0.99)
	TAtomicLatestWins<int32> ConstraintIterations; // Verlet stiffness

	// UNiagaraSystem* from rope's Data Asset (set once at activation, DA keeps it GC-rooted).
	// Stored as void* to avoid UNiagaraSystem include in ECS header.
	std::atomic<void*> NiagaraSystemPtr{nullptr};
};
