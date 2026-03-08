// Per-ability permanent ECS components (always on entity, data ignored when Phase==0).
// Sim-thread owned. Avoids archetype churn from add/remove.

#pragma once

#include "SkeletonTypes.h"
#include "FBarrageKey.h"
#include "FBConstraintParams.h"

struct FSlideState
{
	float CurrentSpeed = 0.f;   // decelerating speed (cm/s)
	float Timer = 0.f;          // remaining duration (seconds)
	float SlideDirX = 0.f;      // Jolt horiz X (normalized, captured first tick)
	float SlideDirZ = 0.f;      // Jolt horiz Z

	void Reset() { CurrentSpeed = Timer = SlideDirX = SlideDirZ = 0.f; }
};

struct FBlinkState
{
	uint8 State = 0;           // 0=Idle, 1=HoldCheck, 2=Aiming
	float HoldTimer = 0.f;
	float TargetX = 0.f, TargetY = 0.f, TargetZ = 0.f; // UE world feet pos
	bool bTargetValid = false;
	bool bPrevBlinkHeld = false; // edge detect (moved from FCharacterSimState)
	bool bTeleportedThisFrame = false; // set by TickBlink, read+cleared by lifecycle manager

	void Reset() { State = 0; HoldTimer = TargetX = TargetY = TargetZ = 0.f; bTargetValid = false; bTeleportedThisFrame = false; }
};

struct FMantleState
{
	float StartX = 0.f, StartY = 0.f, StartZ = 0.f;       // Jolt coords, feet position at phase start
	float EndX = 0.f, EndY = 0.f, EndZ = 0.f;             // Jolt coords, target position for current phase
	float WallNormalX = 0.f, WallNormalZ = 0.f;            // Jolt horizontal wall outward normal
	float Timer = 0.f;                                      // elapsed time in current phase
	float PhaseDuration = 0.f;                               // duration of current phase (seconds)
	float PullEndX = 0.f, PullEndY = 0.f, PullEndZ = 0.f; // Jolt coords, final position after pull
	float PullDuration = 0.f;                                // cached from profile
	float LandDuration = 0.f;                                // cached from profile
	uint8 Phase = 0;     // 0=GrabTransition, 1=Rise, 2=Pull, 3=Land, 4=Hanging
	uint8 MantleType = 0; // 0=Vault, 1=Mantle, 2=LedgeGrab
	bool bCanPullUp = false; // Phase 5 clearance check result (for hang exit routing)

	void Reset()
	{
		StartX = StartY = StartZ = 0.f;
		EndX = EndY = EndZ = 0.f;
		WallNormalX = WallNormalZ = 0.f;
		Timer = PhaseDuration = 0.f;
		PullEndX = PullEndY = PullEndZ = 0.f;
		PullDuration = LandDuration = 0.f;
		Phase = 0; MantleType = 0; bCanPullUp = false;
	}
};

struct FTelekinesisState
{
	FSkeletonKey GrabbedKey;              // skeleton key of grabbed object
	FBarrageKey GrabbedBarrageKey;        // barrage key (cached for physics ops)
	FBarrageKey PivotBarrageKey;          // kinematic pivot body (constraint anchor)
	FBarrageConstraintKey ConstraintKey;  // point constraint between pivot and object
	float OriginalGravityFactor = 1.f;    // restore on release
	float GrabbedMass = 0.f;              // kg, cached at grab time
	float StuckTimer = 0.f;               // accumulates when object can't reach hold point
	float AcquireTimer = 0.f;             // time since grab started
	float SmoothedX = 0.f, SmoothedY = 0.f, SmoothedZ = 0.f; // smoothed hold point (prevents pivot velocity spikes)
	bool bSmoothedInit = false;           // false until first hold point is computed
	uint8 Phase = 0;                      // 0=Idle, 1=Acquiring, 2=Holding

	void Reset()
	{
		GrabbedKey = FSkeletonKey();
		GrabbedBarrageKey = FBarrageKey();
		PivotBarrageKey = FBarrageKey();
		ConstraintKey = FBarrageConstraintKey();
		OriginalGravityFactor = 1.f;
		GrabbedMass = 0.f;
		StuckTimer = AcquireTimer = 0.f;
		SmoothedX = SmoothedY = SmoothedZ = 0.f;
		bSmoothedInit = false;
		Phase = 0;
	}
};
