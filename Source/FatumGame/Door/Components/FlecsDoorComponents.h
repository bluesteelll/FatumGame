// Door components for Flecs entities.
// Static component lives on PREFAB (shared by all doors of same type).
// Instance component lives on each door ENTITY (mutable per-door state).

#pragma once

#include "CoreMinimal.h"

class UFlecsDoorProfile;

// ═══════════════════════════════════════════════════════════════
// DOOR ENUMS
// ═══════════════════════════════════════════════════════════════

enum class EDoorType : uint8
{
	Hinged = 0,
	Sliding = 1
};

enum class EDoorState : uint8
{
	Locked = 0,
	Closed = 1,
	Opening = 2,
	Open = 3,
	Closing = 4
};

// ═══════════════════════════════════════════════════════════════
// DOOR STATIC
// ═══════════════════════════════════════════════════════════════

/**
 * Static door configuration -- lives on PREFAB, shared by all doors of this type.
 * Contains immutable door rules: hinge/slide params, motor tuning, behavior flags.
 *
 * Instance data (State, ConstraintKey, etc.) is in FDoorInstance.
 */
struct FDoorStatic
{
	EDoorType DoorType = EDoorType::Hinged;

	// ── Hinged ──

	/** Maximum open angle in radians (PI/2 = 90 degrees) */
	float MaxOpenAngle = 1.5708f;

	/** Hinge rotation axis (UE coordinates) */
	FVector HingeAxis = FVector(0, 0, 1);

	/** Offset from body center to hinge pivot (cm, local space) */
	FVector HingeOffset = FVector::ZeroVector;

	/** Can open in both directions based on player approach side */
	bool bBidirectional = true;

	// ── Sliding ──

	/** Slide direction in local space (normalized) */
	FVector SlideDirection = FVector(1, 0, 0);

	/** Slide distance in cm */
	float SlideDistance = 200.f;

	// ── Motor ──

	/** Use motor-driven movement (vs. impulse-based) */
	bool bMotorDriven = true;

	/** Motor spring frequency (Hz) */
	float MotorFrequency = 4.f;

	/** Motor damping ratio (1.0 = critical damping) */
	float MotorDamping = 1.0f;

	/** Max torque N*m (hinge) or force N (slider) the motor can apply */
	float MotorMaxTorque = 500.f;

	/** Friction torque N*m (hinge) or force N (slider) when motor is off */
	float FrictionTorque = 5.f;

	// ── Behavior ──

	/** Automatically close after AutoCloseDelay seconds */
	bool bAutoClose = false;

	/** Seconds to wait before auto-closing */
	float AutoCloseDelay = 3.f;

	/** Door starts in Locked state (requires trigger/key to unlock) */
	bool bStartsLocked = false;

	/** Player can unlock via E interaction (if false, only external trigger) */
	bool bUnlockOnInteraction = true;

	/** Latch door at end positions with heavy mass (inertia-based) */
	bool bLockAtEndPosition = false;

	/** Mass (kg) at end positions. Normal Mass restored when moving. */
	float LockMass = 500.f;

	/** Force (N) to break door off hinges. 0 = unbreakable. */
	float ConstraintBreakForce = 0.f;

	/** Torque (Nm) to break door off hinges. 0 = unbreakable. */
	float ConstraintBreakTorque = 0.f;

	// ── Physics tuning ──

	/** Door body mass (kg) */
	float Mass = 25.f;

	/** Angular damping for the door body */
	float AngularDamping = 0.5f;

	static FDoorStatic FromProfile(const UFlecsDoorProfile* Profile);
};

// ═══════════════════════════════════════════════════════════════
// DOOR INSTANCE
// ═══════════════════════════════════════════════════════════════

/**
 * Mutable door state -- per-entity instance.
 * Static data (MaxOpenAngle, motor params, etc.) comes from FDoorStatic in prefab.
 */
struct FDoorInstance
{
	/** Current door state machine state */
	EDoorState State = EDoorState::Closed;

	/** Whether the door has been unlocked (false = locked, requires trigger) */
	bool bUnlocked = true;

	/** Last toggle command state (used to detect state transitions) */
	bool bLastToggleState = false;

	/** Timer for auto-close countdown (seconds remaining) */
	float AutoCloseTimer = 0.f;

	/** Barrage constraint key (stored as int64 to avoid include dependency) */
	int64 ConstraintKey = 0;

	/** Target position: angle in radians (hinge) or distance in Jolt meters (slider) */
	float TargetPosition = 0.f;

	/** Open direction for hinge: +1 or -1 (determined by player approach side) */
	int8 OpenDirection = 1;

	bool IsLocked() const { return State == EDoorState::Locked; }
	bool IsFullyClosed() const { return State == EDoorState::Closed; }
	bool IsFullyOpen() const { return State == EDoorState::Open; }
	bool IsMoving() const { return State == EDoorState::Opening || State == EDoorState::Closing; }
	bool HasConstraint() const { return ConstraintKey != 0; }
};

// ═══════════════════════════════════════════════════════════════
// DOOR TRIGGER LINK
// ═══════════════════════════════════════════════════════════════

/**
 * Links a trigger entity to a door entity for unlock/toggle.
 * Placed on the TRIGGER entity, references the DOOR's SkeletonKey.
 * TargetDoorKey is stored as uint64 to avoid SkeletonTypes.h include.
 */
struct FDoorTriggerLink
{
	/** SkeletonKey of the target door entity (stored as uint64) */
	uint64 TargetDoorKey = 0;

	bool IsValid() const { return TargetDoorKey != 0; }
};
