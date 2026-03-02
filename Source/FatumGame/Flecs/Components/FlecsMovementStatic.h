// Flecs components for sim-thread movement authority.
// FMovementStatic = prefab (read-only params from UFlecsMovementProfile).
// FCharacterMoveState = instance (sprint/posture flags, set via EnqueueCommand).
// FSlideInstance = instance (add/remove dynamically, sim-thread owned).

#pragma once

class UFlecsMovementProfile;

// PREFAB component: one per character type, populated from UFlecsMovementProfile at entity creation.
// Read by PrepareCharacterStep on sim thread.
struct FMovementStatic
{
	/** Create from a UFlecsMovementProfile Data Asset (game thread only). */
	static FMovementStatic FromProfile(const UFlecsMovementProfile* Profile);

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
	// Mantle/Vault params
	float MantleForwardReach = 80.f;         // cm
	float MantleMinHeight = 50.f;            // cm above feet
	float MantleVaultMaxHeight = 120.f;      // cm
	float MantleMaxHeight = 200.f;           // cm
	float MantleRiseDuration = 0.25f;        // seconds
	float MantlePullDuration = 0.3f;
	float MantleLandDuration = 0.1f;
	float VaultSpeedMultiplier = 0.8f;
	// Ledge Grab params
	float LedgeGrabMaxHeight = 236.f;        // cm above feet
	float LedgeGrabTransitionDuration = 0.13f;
	float LedgeGrabMaxDuration = 5.f;        // 0 = infinite
	float WallJumpHorizontalForce = 400.f;   // cm/s
	float WallJumpVerticalForce = 400.f;     // cm/s
	float LedgeGrabCooldown = 0.3f;          // seconds
	float PullUpDuration = 0.45f;            // seconds
	// Ledge detection params (for sim-thread LedgeDetector)
	float LedgeGrabDetectionRadius = 12.f;   // cm, SphereCast probe radius
	float LedgeDetectMaxLookDownAngle = 45.f;// degrees, max look-to-ledge angle
	float LedgeGrabMinLedgeDepth = 15.f;     // cm, Phase 4 depth check
	float StandingEyeHeight = 60.f;          // cm, chest origin for Phase 1
	// Slide→Jump
	float SlideJumpVelocity = 500.f;         // cm/s vertical impulse on slide-cancel jump
	// Jump (sim thread coyote/buffer)
	float CoyoteTimeSeconds = 5.f / 60.f;    // seconds (converted from frames at init)
	float JumpBufferSeconds = 6.f / 60.f;    // seconds
	// Blink params
	float BlinkMaxRange = 1500.f;            // cm
	int32 BlinkMaxCharges = 3;
	float BlinkRechargeTime = 4.f;           // seconds per charge
	float BlinkAimHoldThreshold = 0.15f;     // seconds before aim mode
	float BlinkTargetingSphereRadius = 20.f; // cm, SphereCast probe for targeting
	float BlinkFloorSnapDistance = 200.f;    // cm, max floor snap below air target
	float BlinkLedgeSearchHeight = 350.f;    // cm, wall→ledge-top probe height
	float BlinkMinLedgeDepth = 15.f;         // cm, min ledge depth for wall snap
	bool bBlinkAllowAirTarget = true;        // allow teleport into air
	float BlinkAimTimeDilation = 0.3f;       // time scale during aim
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
	float SlideDirX = 0.f;    // Jolt horizontal X (normalized, captured on first tick)
	float SlideDirZ = 0.f;    // Jolt horizontal Z (normalized, captured on first tick)
};

// INSTANCE component: sim-thread-only state (jump timers, detection timers, prev button states).
// Created once at entity registration, never removed.
struct FCharacterSimState
{
	float CoyoteTimer = 0.f;
	float JumpBufferTimer = 0.f;
	bool bWasGrounded = true;
	bool bPrevCrouchHeld = false;
	bool bPrevBlinkHeld = false;
	// Mantle
	float MantleCooldownTimer = 0.f;
	float AirDetectionTimer = 0.f;   // 10Hz ledge detection while airborne
};

// INSTANCE component: blink ability state (sim-thread FSM + charge system).
// Created once at entity registration, never removed.
struct FBlinkInstance
{
	uint8 State = 0;           // 0=Idle, 1=HoldCheck, 2=Aiming
	float HoldTimer = 0.f;
	int32 Charges = -1;        // -1 = needs lazy init from FMovementStatic
	float RechargeTimer = 0.f;
	float TargetX = 0.f, TargetY = 0.f, TargetZ = 0.f; // UE world feet pos
	bool bTargetValid = false;
};

// INSTANCE component: active mantle/vault/ledge grab.
// Added via EnqueueCommand on activation, removed when complete.
// Sim thread owns position lerp in PrepareCharacterStep.
struct FMantleInstance
{
	float StartX = 0.f, StartY = 0.f, StartZ = 0.f;       // Jolt coords, feet position at phase start
	float EndX = 0.f, EndY = 0.f, EndZ = 0.f;             // Jolt coords, target position for current phase
	float WallNormalX = 0.f, WallNormalZ = 0.f;            // Jolt horizontal wall outward normal
	float Timer = 0.f;                                      // elapsed time in current phase
	float PhaseDuration = 0.f;                               // duration of current phase (seconds)
	float PullEndX = 0.f, PullEndY = 0.f, PullEndZ = 0.f; // Jolt coords, final position after pull (onto ledge top)
	float PullDuration = 0.f;                                // cached from profile
	float LandDuration = 0.f;                                // cached from profile
	uint8 Phase = 0;     // 0=GrabTransition, 1=Rise, 2=Pull, 3=Land, 4=Hanging
	uint8 MantleType = 0; // 0=Vault, 1=Mantle, 2=LedgeGrab
	bool bCanPullUp = false; // Phase 5 clearance check result (for hang exit routing)
};
