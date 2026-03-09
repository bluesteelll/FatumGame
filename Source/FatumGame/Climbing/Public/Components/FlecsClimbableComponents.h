// Climbable entity components (ladders, vines, etc.)
// FClimbableStatic = prefab (immutable params from UFlecsClimbProfile).
// Position/normal computed at activation time from physics body.

#pragma once

// PREFAB component: one per climbable type, populated from UFlecsClimbProfile at entity creation.
// Read by TryActivateClimb on sim thread.
struct FClimbableStatic
{
	float Height = 3.f;                  // total climbable height (Jolt meters)
	float StandoffDist = 0.35f;          // character distance from ladder surface (Jolt meters)
	float ClimbSpeed = 2.0f;             // ascent speed (Jolt m/s)
	float ClimbSpeedDown = 2.5f;         // descent speed (Jolt m/s)
	float JumpOffHorizontalSpeed = 4.0f; // jump-off horizontal (Jolt m/s)
	float JumpOffVerticalSpeed = 3.5f;   // jump-off vertical (Jolt m/s)
	float EnterLerpDuration = 0.15f;     // seconds
	float TopDismountDuration = 0.2f;    // seconds
	float TopDismountForwardDist = 0.5f; // meters forward from ladder top
};

// Tag marking an entity as climbable (zero-size)
struct FTagClimbable
{
};
