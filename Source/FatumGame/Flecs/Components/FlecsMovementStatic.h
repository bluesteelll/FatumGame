// Flecs components for sim-thread movement authority.
// FMovementStatic = prefab (read-only params from UFlecsMovementProfile).
// FCharacterMoveState = instance (sprint/posture flags, set via EnqueueCommand).
// FSlideInstance = instance (add/remove dynamically, sim-thread owned).

#pragma once

// PREFAB component: one per character type, populated from UFlecsMovementProfile at entity creation.
// Read by PrepareCharacterStep on sim thread.
struct FMovementStatic
{
	float WalkSpeed = 300.f;
	float SprintSpeed = 600.f;
	float CrouchSpeed = 150.f;
	float ProneSpeed = 60.f;
	float GroundAcceleration = 2000.f;   // cm/s^2
	float GroundDeceleration = 3000.f;
	float AirAcceleration = 600.f;
	float SprintAcceleration = 2500.f;
	float JumpVelocity = 500.f;          // cm/s
	float CrouchJumpVelocity = 350.f;
	float GravityScale = 1.f;
	float StandingHalfHeight = 88.f;
	float StandingRadius = 34.f;
	float CrouchHalfHeight = 55.f;
	float CrouchRadius = 34.f;
	float ProneHalfHeight = 30.f;
	float ProneRadius = 34.f;
	// Slide params
	float SlideMinEntrySpeed = 400.f;
	float SlideDeceleration = 500.f;     // cm/s^2 linear decel
	float SlideMinExitSpeed = 100.f;
	float SlideMaxDuration = 1.5f;
	float SlideInitialSpeedBoost = 100.f;
	float SlideMinAcceleration = 100.f;  // minimal steering during slide
};

// INSTANCE component: per character, mutable state set via EnqueueCommand from game thread.
// Read by PrepareCharacterStep on sim thread.
struct FCharacterMoveState
{
	bool bSprinting = false;
	uint8 Posture = 0;     // ECharacterPosture (0=Standing, 1=Crouching, 2=Prone)
};

// INSTANCE component: active slide. Added via EnqueueCommand on activation,
// removed by sim thread when exit conditions are met.
// Sim thread owns deceleration/timer logic in PrepareCharacterStep.
struct FSlideInstance
{
	float CurrentSpeed = 0.f;  // decelerating speed (cm/s)
	float Timer = 0.f;         // remaining duration (seconds)
};
