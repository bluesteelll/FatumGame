// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "BarrageConstraintSystem.h"
#include "FWorldSimOwner.h"
#include "CoordinateUtils.h"

// Jolt constraint includes
#include "Jolt/Physics/Constraints/FixedConstraint.h"
#include "Jolt/Physics/Constraints/PointConstraint.h"
#include "Jolt/Physics/Constraints/HingeConstraint.h"
#include "Jolt/Physics/Constraints/SliderConstraint.h"
#include "Jolt/Physics/Constraints/DistanceConstraint.h"
#include "Jolt/Physics/Constraints/ConeConstraint.h"
#include "Jolt/Physics/Body/BodyLockMulti.h"

using namespace JPH;

FBarrageConstraintSystem::FBarrageConstraintSystem(FWorldSimOwner* InOwner)
	: Owner(InOwner)
{
	check(Owner);
}

FBarrageConstraintSystem::~FBarrageConstraintSystem()
{
	Clear();
}

FBarrageConstraintKey FBarrageConstraintSystem::GenerateKey()
{
	return FBarrageConstraintKey(NextConstraintKey++);
}

void FBarrageConstraintSystem::RegisterConstraint(FBarrageConstraintKey Key, const FConstraintData& Data)
{
	Constraints.Add(Key, Data);
	BodyToConstraints.Add(Data.Body1, Key);
	if (Data.Body2.KeyIntoBarrage != 0) // Body2 can be invalid (world constraint)
	{
		BodyToConstraints.Add(Data.Body2, Key);
	}
}

bool FBarrageConstraintSystem::GetJoltBodies(FBarrageKey Key1, FBarrageKey Key2,
											  Body*& OutBody1, Body*& OutBody2) const
{
	if (!Owner || !Owner->physics_system || !Owner->body_interface)
	{
		return false;
	}

	BodyID BodyId1, BodyId2;

	if (!Owner->GetBodyIDOrDefault(Key1, BodyId1) || BodyId1.IsInvalid())
	{
		return false;
	}

	// Body2 can be invalid - in that case we constrain to world
	if (Key2.KeyIntoBarrage != 0)
	{
		if (!Owner->GetBodyIDOrDefault(Key2, BodyId2) || BodyId2.IsInvalid())
		{
			return false;
		}
	}

	// Lock bodies for reading/writing
	BodyLockWrite lock1(Owner->physics_system->GetBodyLockInterface(), BodyId1);
	if (!lock1.Succeeded())
	{
		return false;
	}
	OutBody1 = &lock1.GetBody();

	if (Key2.KeyIntoBarrage != 0)
	{
		BodyLockWrite lock2(Owner->physics_system->GetBodyLockInterface(), BodyId2);
		if (!lock2.Succeeded())
		{
			return false;
		}
		OutBody2 = &lock2.GetBody();
	}
	else
	{
		OutBody2 = nullptr; // Will use Body::sFixedToWorld
	}

	return true;
}

// ============================================================
// Constraint Creation
// ============================================================

FBarrageConstraintKey FBarrageConstraintSystem::CreateFixed(const FBFixedConstraintParams& Params)
{
	if (!Owner || !Owner->physics_system)
	{
		return FBarrageConstraintKey();
	}

	BodyID BodyId1, BodyId2;
	if (!Owner->GetBodyIDOrDefault(Params.Body1, BodyId1) || BodyId1.IsInvalid())
	{
		return FBarrageConstraintKey();
	}

	bool bHasBody2 = Params.Body2.KeyIntoBarrage != 0;
	if (bHasBody2 && (!Owner->GetBodyIDOrDefault(Params.Body2, BodyId2) || BodyId2.IsInvalid()))
	{
		return FBarrageConstraintKey();
	}

	FixedConstraintSettings settings;
	settings.mSpace = Params.Space == EBConstraintSpace::WorldSpace ?
					  EConstraintSpace::WorldSpace : EConstraintSpace::LocalToBodyCOM;
	settings.mAutoDetectPoint = Params.bAutoDetectAnchor;

	if (!Params.bAutoDetectAnchor)
	{
		settings.mPoint1 = CoordinateUtils::ToJoltCoordinates(Params.AnchorPoint1);
		settings.mPoint2 = CoordinateUtils::ToJoltCoordinates(Params.AnchorPoint2);
	}

	// Create constraint with body locks
	Ref<Constraint> constraint;
	{
		BodyID bodyIds[2] = { BodyId1, bHasBody2 ? BodyId2 : BodyID() };
		int bodyCount = bHasBody2 ? 2 : 1;
		BodyLockMultiWrite lock(Owner->physics_system->GetBodyLockInterface(), bodyIds, bodyCount);

		Body* body1Ptr = lock.GetBody(0);
		Body* body2Ptr = bHasBody2 ? lock.GetBody(1) : nullptr;

		if (!body1Ptr)
		{
			return FBarrageConstraintKey();
		}

		Body& body1 = *body1Ptr;
		Body& body2 = body2Ptr ? *body2Ptr : Body::sFixedToWorld;

		constraint = settings.Create(body1, body2);
	}

	Owner->physics_system->AddConstraint(constraint.GetPtr());

	FBarrageConstraintKey key = GenerateKey();
	FConstraintData data;
	data.JoltConstraint = constraint;
	data.Body1 = Params.Body1;
	data.Body2 = Params.Body2;
	data.BreakForce = Params.BreakForce;
	data.BreakTorque = Params.BreakTorque;
	data.UserData = Params.UserData;

	RegisterConstraint(key, data);
	return key;
}

FBarrageConstraintKey FBarrageConstraintSystem::CreatePoint(const FBPointConstraintParams& Params)
{
	if (!Owner || !Owner->physics_system)
	{
		return FBarrageConstraintKey();
	}

	BodyID BodyId1, BodyId2;
	if (!Owner->GetBodyIDOrDefault(Params.Body1, BodyId1) || BodyId1.IsInvalid())
	{
		return FBarrageConstraintKey();
	}

	bool bHasBody2 = Params.Body2.KeyIntoBarrage != 0;
	if (bHasBody2 && (!Owner->GetBodyIDOrDefault(Params.Body2, BodyId2) || BodyId2.IsInvalid()))
	{
		return FBarrageConstraintKey();
	}

	PointConstraintSettings settings;
	settings.mSpace = Params.Space == EBConstraintSpace::WorldSpace ?
					  EConstraintSpace::WorldSpace : EConstraintSpace::LocalToBodyCOM;

	settings.mPoint1 = CoordinateUtils::ToJoltCoordinates(Params.AnchorPoint1);
	settings.mPoint2 = CoordinateUtils::ToJoltCoordinates(Params.AnchorPoint2);

	Ref<Constraint> constraint;
	{
		BodyID bodyIds[2] = { BodyId1, bHasBody2 ? BodyId2 : BodyID() };
		int bodyCount = bHasBody2 ? 2 : 1;
		BodyLockMultiWrite lock(Owner->physics_system->GetBodyLockInterface(), bodyIds, bodyCount);

		Body* body1Ptr = lock.GetBody(0);
		Body* body2Ptr = bHasBody2 ? lock.GetBody(1) : nullptr;

		if (!body1Ptr)
		{
			return FBarrageConstraintKey();
		}

		Body& body1 = *body1Ptr;
		Body& body2 = body2Ptr ? *body2Ptr : Body::sFixedToWorld;

		constraint = settings.Create(body1, body2);
	}

	Owner->physics_system->AddConstraint(constraint.GetPtr());

	FBarrageConstraintKey key = GenerateKey();
	FConstraintData data;
	data.JoltConstraint = constraint;
	data.Body1 = Params.Body1;
	data.Body2 = Params.Body2;
	data.BreakForce = Params.BreakForce;
	data.BreakTorque = Params.BreakTorque;
	data.UserData = Params.UserData;

	RegisterConstraint(key, data);
	return key;
}

FBarrageConstraintKey FBarrageConstraintSystem::CreateHinge(const FBHingeConstraintParams& Params)
{
	if (!Owner || !Owner->physics_system)
	{
		return FBarrageConstraintKey();
	}

	BodyID BodyId1, BodyId2;
	if (!Owner->GetBodyIDOrDefault(Params.Body1, BodyId1) || BodyId1.IsInvalid())
	{
		return FBarrageConstraintKey();
	}

	bool bHasBody2 = Params.Body2.KeyIntoBarrage != 0;
	if (bHasBody2 && (!Owner->GetBodyIDOrDefault(Params.Body2, BodyId2) || BodyId2.IsInvalid()))
	{
		return FBarrageConstraintKey();
	}

	HingeConstraintSettings settings;
	settings.mSpace = Params.Space == EBConstraintSpace::WorldSpace ?
					  EConstraintSpace::WorldSpace : EConstraintSpace::LocalToBodyCOM;

	settings.mPoint1 = CoordinateUtils::ToJoltCoordinates(Params.AnchorPoint1);
	settings.mPoint2 = CoordinateUtils::ToJoltCoordinates(Params.AnchorPoint2);
	settings.mHingeAxis1 = CoordinateUtils::ToJoltUnitVector(Params.HingeAxis);
	settings.mHingeAxis2 = settings.mHingeAxis1;
	settings.mNormalAxis1 = CoordinateUtils::ToJoltUnitVector(Params.NormalAxis);
	settings.mNormalAxis2 = settings.mNormalAxis1;

	if (Params.bHasLimits)
	{
		settings.mLimitsMin = Params.MinAngle;
		settings.mLimitsMax = Params.MaxAngle;
	}

	Ref<Constraint> constraint;
	{
		BodyID bodyIds[2] = { BodyId1, bHasBody2 ? BodyId2 : BodyID() };
		int bodyCount = bHasBody2 ? 2 : 1;
		BodyLockMultiWrite lock(Owner->physics_system->GetBodyLockInterface(), bodyIds, bodyCount);

		Body* body1Ptr = lock.GetBody(0);
		Body* body2Ptr = bHasBody2 ? lock.GetBody(1) : nullptr;

		if (!body1Ptr)
		{
			return FBarrageConstraintKey();
		}

		Body& body1 = *body1Ptr;
		Body& body2 = body2Ptr ? *body2Ptr : Body::sFixedToWorld;

		constraint = settings.Create(body1, body2);

		// Set up motor if requested
		if (Params.bEnableMotor)
		{
			HingeConstraint* hinge = static_cast<HingeConstraint*>(constraint.GetPtr());
			hinge->SetMotorState(EMotorState::Velocity);
			hinge->SetTargetAngularVelocity(Params.MotorTargetVelocity);
			MotorSettings& motor = hinge->GetMotorSettings();
			motor.mMaxTorqueLimit = Params.MotorMaxTorque;
			motor.mMinTorqueLimit = -Params.MotorMaxTorque;
		}
	}

	Owner->physics_system->AddConstraint(constraint.GetPtr());

	FBarrageConstraintKey key = GenerateKey();
	FConstraintData data;
	data.JoltConstraint = constraint;
	data.Body1 = Params.Body1;
	data.Body2 = Params.Body2;
	data.BreakForce = Params.BreakForce;
	data.BreakTorque = Params.BreakTorque;
	data.UserData = Params.UserData;

	RegisterConstraint(key, data);
	return key;
}

FBarrageConstraintKey FBarrageConstraintSystem::CreateSlider(const FBSliderConstraintParams& Params)
{
	if (!Owner || !Owner->physics_system)
	{
		return FBarrageConstraintKey();
	}

	BodyID BodyId1, BodyId2;
	if (!Owner->GetBodyIDOrDefault(Params.Body1, BodyId1) || BodyId1.IsInvalid())
	{
		return FBarrageConstraintKey();
	}

	bool bHasBody2 = Params.Body2.KeyIntoBarrage != 0;
	if (bHasBody2 && (!Owner->GetBodyIDOrDefault(Params.Body2, BodyId2) || BodyId2.IsInvalid()))
	{
		return FBarrageConstraintKey();
	}

	SliderConstraintSettings settings;
	settings.mSpace = Params.Space == EBConstraintSpace::WorldSpace ?
					  EConstraintSpace::WorldSpace : EConstraintSpace::LocalToBodyCOM;

	settings.mPoint1 = CoordinateUtils::ToJoltCoordinates(Params.AnchorPoint1);
	settings.mPoint2 = CoordinateUtils::ToJoltCoordinates(Params.AnchorPoint2);
	settings.mSliderAxis1 = CoordinateUtils::ToJoltUnitVector(Params.SliderAxis);
	settings.mSliderAxis2 = settings.mSliderAxis1;
	settings.mNormalAxis1 = CoordinateUtils::ToJoltUnitVector(Params.NormalAxis);
	settings.mNormalAxis2 = settings.mNormalAxis1;

	if (Params.bHasLimits)
	{
		// Convert from UE units (cm) to Jolt units (m)
		settings.mLimitsMin = Params.MinLimit / 100.0f;
		settings.mLimitsMax = Params.MaxLimit / 100.0f;
	}

	Ref<Constraint> constraint;
	{
		BodyID bodyIds[2] = { BodyId1, bHasBody2 ? BodyId2 : BodyID() };
		int bodyCount = bHasBody2 ? 2 : 1;
		BodyLockMultiWrite lock(Owner->physics_system->GetBodyLockInterface(), bodyIds, bodyCount);

		Body* body1Ptr = lock.GetBody(0);
		Body* body2Ptr = bHasBody2 ? lock.GetBody(1) : nullptr;

		if (!body1Ptr)
		{
			return FBarrageConstraintKey();
		}

		Body& body1 = *body1Ptr;
		Body& body2 = body2Ptr ? *body2Ptr : Body::sFixedToWorld;

		constraint = settings.Create(body1, body2);

		// Set up motor if requested
		if (Params.bEnableMotor)
		{
			SliderConstraint* slider = static_cast<SliderConstraint*>(constraint.GetPtr());
			slider->SetMotorState(EMotorState::Velocity);
			slider->SetTargetVelocity(Params.MotorTargetVelocity / 100.0f); // Convert to m/s
			MotorSettings& motor = slider->GetMotorSettings();
			motor.mMaxForceLimit = Params.MotorMaxForce;
			motor.mMinForceLimit = -Params.MotorMaxForce;
		}
	}

	Owner->physics_system->AddConstraint(constraint.GetPtr());

	FBarrageConstraintKey key = GenerateKey();
	FConstraintData data;
	data.JoltConstraint = constraint;
	data.Body1 = Params.Body1;
	data.Body2 = Params.Body2;
	data.BreakForce = Params.BreakForce;
	data.BreakTorque = Params.BreakTorque;
	data.UserData = Params.UserData;

	RegisterConstraint(key, data);
	return key;
}

FBarrageConstraintKey FBarrageConstraintSystem::CreateDistance(const FBDistanceConstraintParams& Params)
{
	if (!Owner || !Owner->physics_system)
	{
		return FBarrageConstraintKey();
	}

	BodyID BodyId1, BodyId2;
	if (!Owner->GetBodyIDOrDefault(Params.Body1, BodyId1) || BodyId1.IsInvalid())
	{
		return FBarrageConstraintKey();
	}

	bool bHasBody2 = Params.Body2.KeyIntoBarrage != 0;
	if (bHasBody2 && (!Owner->GetBodyIDOrDefault(Params.Body2, BodyId2) || BodyId2.IsInvalid()))
	{
		return FBarrageConstraintKey();
	}

	DistanceConstraintSettings settings;
	settings.mSpace = Params.Space == EBConstraintSpace::WorldSpace ?
					  EConstraintSpace::WorldSpace : EConstraintSpace::LocalToBodyCOM;

	settings.mPoint1 = CoordinateUtils::ToJoltCoordinates(Params.AnchorPoint1);
	settings.mPoint2 = CoordinateUtils::ToJoltCoordinates(Params.AnchorPoint2);

	// Convert distances from UE (cm) to Jolt (m)
	settings.mMinDistance = Params.MinDistance / 100.0f;
	settings.mMaxDistance = Params.MaxDistance > 0.0f ? Params.MaxDistance / 100.0f : -1.0f; // -1 = auto-detect

	// Set up spring if requested
	if (Params.SpringFrequency > 0.0f)
	{
		settings.mLimitsSpringSettings.mMode = ESpringMode::FrequencyAndDamping;
		settings.mLimitsSpringSettings.mFrequency = Params.SpringFrequency;
		settings.mLimitsSpringSettings.mDamping = Params.SpringDamping;
	}

	Ref<Constraint> constraint;
	{
		BodyID bodyIds[2] = { BodyId1, bHasBody2 ? BodyId2 : BodyID() };
		int bodyCount = bHasBody2 ? 2 : 1;
		BodyLockMultiWrite lock(Owner->physics_system->GetBodyLockInterface(), bodyIds, bodyCount);

		Body* body1Ptr = lock.GetBody(0);
		Body* body2Ptr = bHasBody2 ? lock.GetBody(1) : nullptr;

		if (!body1Ptr)
		{
			return FBarrageConstraintKey();
		}

		Body& body1 = *body1Ptr;
		Body& body2 = body2Ptr ? *body2Ptr : Body::sFixedToWorld;

		constraint = settings.Create(body1, body2);
	}

	Owner->physics_system->AddConstraint(constraint.GetPtr());

	FBarrageConstraintKey key = GenerateKey();
	FConstraintData data;
	data.JoltConstraint = constraint;
	data.Body1 = Params.Body1;
	data.Body2 = Params.Body2;
	data.BreakForce = Params.BreakForce;
	data.BreakTorque = Params.BreakTorque;
	data.UserData = Params.UserData;

	RegisterConstraint(key, data);
	return key;
}

FBarrageConstraintKey FBarrageConstraintSystem::CreateCone(const FBConeConstraintParams& Params)
{
	if (!Owner || !Owner->physics_system)
	{
		return FBarrageConstraintKey();
	}

	BodyID BodyId1, BodyId2;
	if (!Owner->GetBodyIDOrDefault(Params.Body1, BodyId1) || BodyId1.IsInvalid())
	{
		return FBarrageConstraintKey();
	}

	bool bHasBody2 = Params.Body2.KeyIntoBarrage != 0;
	if (bHasBody2 && (!Owner->GetBodyIDOrDefault(Params.Body2, BodyId2) || BodyId2.IsInvalid()))
	{
		return FBarrageConstraintKey();
	}

	ConeConstraintSettings settings;
	settings.mSpace = Params.Space == EBConstraintSpace::WorldSpace ?
					  EConstraintSpace::WorldSpace : EConstraintSpace::LocalToBodyCOM;

	settings.mPoint1 = CoordinateUtils::ToJoltCoordinates(Params.AnchorPoint1);
	settings.mPoint2 = CoordinateUtils::ToJoltCoordinates(Params.AnchorPoint2);
	settings.mTwistAxis1 = CoordinateUtils::ToJoltUnitVector(Params.ConeAxis);
	settings.mTwistAxis2 = settings.mTwistAxis1;
	settings.mHalfConeAngle = Params.HalfConeAngle;

	Ref<Constraint> constraint;
	{
		BodyID bodyIds[2] = { BodyId1, bHasBody2 ? BodyId2 : BodyID() };
		int bodyCount = bHasBody2 ? 2 : 1;
		BodyLockMultiWrite lock(Owner->physics_system->GetBodyLockInterface(), bodyIds, bodyCount);

		Body* body1Ptr = lock.GetBody(0);
		Body* body2Ptr = bHasBody2 ? lock.GetBody(1) : nullptr;

		if (!body1Ptr)
		{
			return FBarrageConstraintKey();
		}

		Body& body1 = *body1Ptr;
		Body& body2 = body2Ptr ? *body2Ptr : Body::sFixedToWorld;

		constraint = settings.Create(body1, body2);
	}

	Owner->physics_system->AddConstraint(constraint.GetPtr());

	FBarrageConstraintKey key = GenerateKey();
	FConstraintData data;
	data.JoltConstraint = constraint;
	data.Body1 = Params.Body1;
	data.Body2 = Params.Body2;
	data.BreakForce = Params.BreakForce;
	data.BreakTorque = Params.BreakTorque;
	data.UserData = Params.UserData;

	RegisterConstraint(key, data);
	return key;
}

// ============================================================
// Constraint Management
// ============================================================

bool FBarrageConstraintSystem::Remove(FBarrageConstraintKey Key)
{
	FConstraintData* Data = Constraints.Find(Key);
	if (!Data)
	{
		return false;
	}

	// Remove from physics system
	if (Owner && Owner->physics_system && Data->JoltConstraint)
	{
		Owner->physics_system->RemoveConstraint(Data->JoltConstraint.GetPtr());
	}

	// Remove from reverse lookup
	BodyToConstraints.Remove(Data->Body1, Key);
	if (Data->Body2.KeyIntoBarrage != 0)
	{
		BodyToConstraints.Remove(Data->Body2, Key);
	}

	// Remove from main map
	Constraints.Remove(Key);
	return true;
}

int32 FBarrageConstraintSystem::RemoveAllForBody(FBarrageKey BodyKey)
{
	TArray<FBarrageConstraintKey> KeysToRemove;
	BodyToConstraints.MultiFind(BodyKey, KeysToRemove);

	for (const FBarrageConstraintKey& Key : KeysToRemove)
	{
		Remove(Key);
	}

	return KeysToRemove.Num();
}

void FBarrageConstraintSystem::SetEnabled(FBarrageConstraintKey Key, bool bEnabled)
{
	FConstraintData* Data = Constraints.Find(Key);
	if (Data && Data->JoltConstraint)
	{
		Data->JoltConstraint->SetEnabled(bEnabled);
	}
}

bool FBarrageConstraintSystem::IsEnabled(FBarrageConstraintKey Key) const
{
	const FConstraintData* Data = Constraints.Find(Key);
	if (Data && Data->JoltConstraint)
	{
		return Data->JoltConstraint->GetEnabled();
	}
	return false;
}

bool FBarrageConstraintSystem::IsValid(FBarrageConstraintKey Key) const
{
	return Constraints.Contains(Key);
}

// ============================================================
// Force Queries & Breaking
// ============================================================

bool FBarrageConstraintSystem::GetForces(FBarrageConstraintKey Key, FBConstraintForces& OutForces) const
{
	const FConstraintData* Data = Constraints.Find(Key);
	if (!Data || !Data->JoltConstraint)
	{
		return false;
	}

	// Different constraint types have different ways to get forces
	// For now, we use the TwoBodyConstraint interface where available
	if (Data->JoltConstraint->GetType() == EConstraintType::TwoBodyConstraint)
	{
		const TwoBodyConstraint* tbc = static_cast<const TwoBodyConstraint*>(Data->JoltConstraint.GetPtr());

		// Get constraint impulses (lambda values) and convert to forces
		// The exact method depends on constraint type
		switch (Data->JoltConstraint->GetSubType())
		{
		case EConstraintSubType::Fixed:
			{
				const FixedConstraint* fc = static_cast<const FixedConstraint*>(tbc);
				Vec3 posLambda = fc->GetTotalLambdaPosition();
				Vec3 rotLambda = fc->GetTotalLambdaRotation();

				// Lambda is impulse, divide by delta time to get force (approximate)
				float dt = Owner ? Owner->DeltaTime : 0.01f;
				OutForces.LinearForce = CoordinateUtils::FromJoltCoordinatesD(posLambda / dt);
				OutForces.AngularTorque = CoordinateUtils::FromJoltCoordinatesD(rotLambda / dt);
			}
			break;

		case EConstraintSubType::Point:
			{
				const PointConstraint* pc = static_cast<const PointConstraint*>(tbc);
				Vec3 lambda = pc->GetTotalLambdaPosition();

				float dt = Owner ? Owner->DeltaTime : 0.01f;
				OutForces.LinearForce = CoordinateUtils::FromJoltCoordinatesD(lambda / dt);
				OutForces.AngularTorque = FVector3d::ZeroVector;
			}
			break;

		case EConstraintSubType::Hinge:
			{
				const HingeConstraint* hc = static_cast<const HingeConstraint*>(tbc);
				Vec3 lambda1 = hc->GetTotalLambdaPosition();
				// GetTotalLambdaRotation returns Vector<2> for hinge constraints
				Vector<2> lambda2 = hc->GetTotalLambdaRotation();
				float lambdaLimits = hc->GetTotalLambdaRotationLimits();

				float dt = Owner ? Owner->DeltaTime : 0.01f;
				OutForces.LinearForce = CoordinateUtils::FromJoltCoordinatesD(lambda1 / dt);
				// Convert Vector<2> to torque (hinge only rotates around one axis)
				OutForces.AngularTorque = FVector3d(lambda2[0] / dt, lambda2[1] / dt, lambdaLimits / dt);
			}
			break;

		default:
			// For other types, approximate with zero (user can extend)
			OutForces.LinearForce = FVector3d::ZeroVector;
			OutForces.AngularTorque = FVector3d::ZeroVector;
			break;
		}
	}

	return true;
}

bool FBarrageConstraintSystem::ShouldBreak(FBarrageConstraintKey Key) const
{
	const FConstraintData* Data = Constraints.Find(Key);
	if (!Data)
	{
		return false;
	}

	// If no break thresholds are set, never break
	if (Data->BreakForce <= 0.0f && Data->BreakTorque <= 0.0f)
	{
		return false;
	}

	FBConstraintForces forces;
	if (!GetForces(Key, forces))
	{
		return false;
	}

	// Check force threshold
	if (Data->BreakForce > 0.0f && forces.GetForceMagnitude() > Data->BreakForce)
	{
		return true;
	}

	// Check torque threshold
	if (Data->BreakTorque > 0.0f && forces.GetTorqueMagnitude() > Data->BreakTorque)
	{
		return true;
	}

	return false;
}

int32 FBarrageConstraintSystem::ProcessBreakableConstraints(TArray<FBarrageConstraintKey>* OutBrokenConstraints)
{
	TArray<FBarrageConstraintKey> ToBreak;

	for (const auto& Pair : Constraints)
	{
		if (ShouldBreak(Pair.Key))
		{
			ToBreak.Add(Pair.Key);
		}
	}

	for (const FBarrageConstraintKey& Key : ToBreak)
	{
		Remove(Key);
		if (OutBrokenConstraints)
		{
			OutBrokenConstraints->Add(Key);
		}
	}

	return ToBreak.Num();
}

// ============================================================
// Bulk Operations
// ============================================================

void FBarrageConstraintSystem::GetAllConstraints(TArray<FBarrageConstraintKey>& OutKeys) const
{
	Constraints.GetKeys(OutKeys);
}

void FBarrageConstraintSystem::GetConstraintsForBody(FBarrageKey BodyKey, TArray<FBarrageConstraintKey>& OutKeys) const
{
	BodyToConstraints.MultiFind(BodyKey, OutKeys);
}

int32 FBarrageConstraintSystem::GetConstraintCount() const
{
	return Constraints.Num();
}

void FBarrageConstraintSystem::Clear()
{
	// Remove all constraints from physics system
	if (Owner && Owner->physics_system)
	{
		for (auto& Pair : Constraints)
		{
			if (Pair.Value.JoltConstraint)
			{
				Owner->physics_system->RemoveConstraint(Pair.Value.JoltConstraint.GetPtr());
			}
		}
	}

	Constraints.Empty();
	BodyToConstraints.Empty();
}
