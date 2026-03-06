// FMovementStatic::FromProfile — converts UFlecsMovementProfile (UObject) to sim-thread struct.

#include "FlecsMovementStatic.h"
#include "FlecsMovementProfile.h"

FMovementStatic FMovementStatic::FromProfile(const UFlecsMovementProfile* Profile)
{
	check(Profile);

	FMovementStatic MS;
	// Speed
	MS.WalkSpeed = Profile->WalkSpeed;
	MS.SprintSpeed = Profile->SprintSpeed;
	MS.CrouchSpeed = Profile->CrouchSpeed;
	MS.ProneSpeed = Profile->ProneSpeed;
	// Acceleration
	MS.GroundAcceleration = Profile->GroundAcceleration;
	MS.GroundDeceleration = Profile->GroundDeceleration;
	MS.AirAcceleration = Profile->AirAcceleration;
	MS.SprintAcceleration = Profile->SprintAcceleration;
	// Jump
	MS.JumpVelocity = Profile->JumpVelocity;
	MS.CrouchJumpVelocity = Profile->CrouchJumpVelocity;
	MS.GravityScale = Profile->GravityScale;
	// Capsule
	MS.StandingHalfHeight = Profile->StandingHalfHeight;
	MS.StandingRadius = Profile->StandingRadius;
	MS.CrouchHalfHeight = Profile->CrouchHalfHeight;
	MS.CrouchRadius = Profile->CrouchRadius;
	MS.ProneHalfHeight = Profile->ProneHalfHeight;
	MS.ProneRadius = Profile->ProneRadius;
	// Slide
	MS.SlideMinEntrySpeed = Profile->SlideMinEntrySpeed;
	MS.SlideDeceleration = Profile->SlideDeceleration;
	MS.SlideMinExitSpeed = Profile->SlideMinExitSpeed;
	MS.SlideMaxDuration = Profile->SlideMaxDuration;
	MS.SlideInitialSpeedBoost = Profile->SlideInitialSpeedBoost;
	MS.SlideMinAcceleration = Profile->SlideMinAcceleration;
	// Mantle/Vault
	MS.MantleForwardReach = Profile->MantleForwardReach;
	MS.MantleMinHeight = Profile->MantleMinHeight;
	MS.MantleVaultMaxHeight = Profile->MantleVaultMaxHeight;
	MS.MantleMaxHeight = Profile->MantleMaxHeight;
	MS.MantleRiseDuration = Profile->MantleRiseDuration;
	MS.MantlePullDuration = Profile->MantlePullDuration;
	MS.MantleLandDuration = Profile->MantleLandDuration;
	MS.VaultSpeedMultiplier = Profile->VaultSpeedMultiplier;
	// Ledge Grab
	MS.LedgeGrabMaxHeight = Profile->LedgeGrabMaxHeight;
	MS.LedgeGrabTransitionDuration = Profile->LedgeGrabTransitionDuration;
	MS.LedgeGrabMaxDuration = Profile->bUseLedgeHangTimeout ? Profile->LedgeHangMaxDuration : 0.f;
	MS.WallJumpHorizontalForce = Profile->WallJumpHorizontalForce;
	MS.WallJumpVerticalForce = Profile->WallJumpVerticalForce;
	MS.LedgeGrabCooldown = Profile->LedgeGrabCooldown;
	MS.PullUpDuration = Profile->PullUpDuration;
	// Ledge detection (for sim-thread LedgeDetector)
	MS.LedgeGrabDetectionRadius = Profile->LedgeGrabDetectionRadius;
	MS.LedgeDetectMaxLookDownAngle = Profile->LedgeDetectMaxLookDownAngle;
	MS.LedgeGrabMinLedgeDepth = Profile->LedgeGrabMinLedgeDepth;
	MS.StandingEyeHeight = Profile->StandingEyeHeight;
	// Slide→Jump
	MS.SlideJumpVelocity = Profile->SlideJumpVelocity;
	// Jump (convert frames to seconds at 60Hz)
	MS.CoyoteTimeSeconds = Profile->CoyoteTimeFrames / 60.f;
	MS.JumpBufferSeconds = Profile->JumpBufferFrames / 60.f;
	// Blink
	MS.BlinkMaxRange = Profile->BlinkMaxRange;
	MS.BlinkMaxCharges = Profile->BlinkMaxCharges;
	MS.BlinkRechargeTime = Profile->BlinkRechargeTime;
	MS.BlinkAimHoldThreshold = Profile->BlinkAimHoldThreshold;
	MS.BlinkTargetingSphereRadius = Profile->BlinkTargetingSphereRadius;
	MS.BlinkFloorSnapDistance = Profile->BlinkFloorSnapDistance;
	MS.BlinkLedgeSearchHeight = Profile->BlinkLedgeSearchHeight;
	MS.BlinkMinLedgeDepth = Profile->BlinkMinLedgeDepth;
	MS.bBlinkAllowAirTarget = Profile->bBlinkAllowAirTarget;
	MS.BlinkAimTimeDilation = Profile->BlinkAimTimeDilation;
	return MS;
}
