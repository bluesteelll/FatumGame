// FDoorStatic::FromProfile — converts UFlecsDoorProfile (UObject) to sim-thread struct.

#include "FlecsDoorComponents.h"
#include "FlecsDoorProfile.h"

FDoorStatic FDoorStatic::FromProfile(const UFlecsDoorProfile* Profile)
{
	check(Profile);

	FDoorStatic S;
	S.DoorType = Profile->IsHinged() ? EDoorType::Hinged : EDoorType::Sliding;

	// Hinged
	S.MaxOpenAngle = Profile->GetMaxOpenAngleRadians();
	S.HingeAxis = Profile->HingeAxis.GetSafeNormal();
	S.HingeOffset = Profile->HingeOffset;
	S.bBidirectional = Profile->bBidirectional;

	// Sliding
	S.SlideDirection = Profile->SlideDirection.GetSafeNormal();
	S.SlideDistance = Profile->SlideDistanceCm;

	// Motor
	S.bMotorDriven = Profile->bMotorDriven;
	S.MotorFrequency = Profile->MotorFrequency;
	S.MotorDamping = Profile->MotorDamping;
	S.MotorMaxTorque = Profile->MotorMaxForce;
	S.FrictionTorque = Profile->FrictionForce;

	// Behavior
	S.bAutoClose = Profile->bAutoClose;
	S.AutoCloseDelay = Profile->AutoCloseDelay;
	S.bStartsLocked = Profile->bStartsLocked;
	S.bUnlockOnInteraction = Profile->bUnlockOnInteraction;
	S.bLockAtEndPosition = Profile->bLockAtEndPosition;
	S.LockMass = Profile->LockMass;
	S.ConstraintBreakForce = Profile->ConstraintBreakForce;
	S.ConstraintBreakTorque = Profile->ConstraintBreakTorque;

	// Physics
	S.Mass = Profile->Mass;
	S.AngularDamping = Profile->AngularDamping;

	return S;
}
