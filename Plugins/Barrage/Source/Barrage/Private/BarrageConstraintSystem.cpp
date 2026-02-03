
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

		// Get body positions for distance calculation
		Vec3 pos1 = body1.GetCenterOfMassPosition();
		Vec3 pos2 = body2.GetID().IsInvalid() ? Vec3::sZero() : body2.GetCenterOfMassPosition();
		Vec3 axis = pos2 - pos1;
		float currentDistance = axis.Length();

		// If bLockRotation is true, use SliderConstraint instead (locks rotation, allows linear movement)
		if (Params.bLockRotation)
		{
			SliderConstraintSettings settings;
			settings.mSpace = EConstraintSpace::WorldSpace;
			settings.mPoint1 = pos1;
			settings.mPoint2 = pos2;

			// Slider axis is the direction from body1 to body2
			if (currentDistance > 0.001f)
			{
				settings.mSliderAxis1 = axis.Normalized();
			}
			else
			{
				settings.mSliderAxis1 = Vec3(1, 0, 0); // Fallback
			}
			settings.mSliderAxis2 = settings.mSliderAxis1;

			// Calculate normal axis perpendicular to slider
			Vec3 up(0, 0, 1);
			if (FMath::Abs(settings.mSliderAxis1.Dot(up)) > 0.99f)
			{
				up = Vec3(1, 0, 0); // Use different up if axis is nearly vertical
			}
			settings.mNormalAxis1 = settings.mSliderAxis1.Cross(up).Normalized();
			settings.mNormalAxis2 = settings.mNormalAxis1;

			// Convert distances from UE (cm) to Jolt (m)
			float minDist = Params.MinDistance / 100.0f;
			float maxDist = Params.MaxDistance > 0.0f ? Params.MaxDistance / 100.0f : currentDistance;
			if (Params.MinDistance <= 0.0f)
			{
				minDist = currentDistance;
			}

			// Slider limits are relative to initial position
			// So we set limits around 0 based on min/max distances
			settings.mLimitsMin = minDist - currentDistance;
			settings.mLimitsMax = maxDist - currentDistance;

			// Set up spring if requested
			if (Params.SpringFrequency > 0.0f)
			{
				settings.mLimitsSpringSettings.mMode = ESpringMode::FrequencyAndDamping;
				settings.mLimitsSpringSettings.mFrequency = Params.SpringFrequency;
				settings.mLimitsSpringSettings.mDamping = Params.SpringDamping;

				UE_LOG(LogTemp, Warning, TEXT("SliderConstraint (LockRotation) SPRING: Frequency=%.2f Hz, Damping=%.2f"),
					Params.SpringFrequency, Params.SpringDamping);
			}

			constraint = settings.Create(body1, body2);

			UE_LOG(LogTemp, Warning, TEXT("SliderConstraint (LockRotation) CREATED:"));
			UE_LOG(LogTemp, Warning, TEXT("  Limits: [%.2f, %.2f] m relative to start"),
				settings.mLimitsMin, settings.mLimitsMax);
		}
		else
		{
			// Standard DistanceConstraint
			DistanceConstraintSettings settings;

			// For distance constraint, we use LocalToBodyCOM space and auto-detect anchor points
			// This means anchor points are at the center of each body
			if (Params.bAutoDetectAnchor)
			{
				settings.mSpace = EConstraintSpace::LocalToBodyCOM;
				settings.mPoint1 = Vec3::sZero();
				settings.mPoint2 = Vec3::sZero();
			}
			else
			{
				settings.mSpace = Params.Space == EBConstraintSpace::WorldSpace ?
								  EConstraintSpace::WorldSpace : EConstraintSpace::LocalToBodyCOM;
				settings.mPoint1 = CoordinateUtils::ToJoltCoordinates(Params.AnchorPoint1);
				settings.mPoint2 = CoordinateUtils::ToJoltCoordinates(Params.AnchorPoint2);
			}

			// Convert distances from UE (cm) to Jolt (m)
			settings.mMinDistance = Params.MinDistance / 100.0f;

			// If MaxDistance is 0, auto-detect from current body positions
			if (Params.MaxDistance > 0.0f)
			{
				settings.mMaxDistance = Params.MaxDistance / 100.0f;
			}
			else
			{
				settings.mMaxDistance = currentDistance;
				// Also set min to same for rigid rope behavior
				if (Params.MinDistance <= 0.0f)
				{
					settings.mMinDistance = currentDistance;
				}
			}

			UE_LOG(LogTemp, Log, TEXT("DistanceConstraint: MinDist=%.2f m, MaxDist=%.2f m (input: %.0f cm, %.0f cm)"),
				settings.mMinDistance, settings.mMaxDistance, Params.MinDistance, Params.MaxDistance);

			// Set up spring if requested
			// Spring makes the constraint "soft" - bodies can stretch beyond limits and spring back
			if (Params.SpringFrequency > 0.0f)
			{
				// Use FrequencyAndDamping mode - more intuitive for Jolt
				// Frequency is in Hz (oscillations per second)
				// Damping ratio: 0 = no damping (bounces forever), 1 = critical damping (no oscillation)
				settings.mLimitsSpringSettings.mMode = ESpringMode::FrequencyAndDamping;
				settings.mLimitsSpringSettings.mFrequency = Params.SpringFrequency;
				settings.mLimitsSpringSettings.mDamping = Params.SpringDamping;

				UE_LOG(LogTemp, Warning, TEXT("DistanceConstraint SPRING: Frequency=%.2f Hz, Damping=%.2f"),
					Params.SpringFrequency, Params.SpringDamping);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("DistanceConstraint: NO SPRING (Frequency=0)"));
			}

			constraint = settings.Create(body1, body2);

			// Debug: log constraint creation and body types
			float actualDist = currentDistance;

			UE_LOG(LogTemp, Warning, TEXT("DistanceConstraint CREATED:"));
			UE_LOG(LogTemp, Warning, TEXT("  Body1: valid=%d, dynamic=%d, pos=(%.1f, %.1f, %.1f)"),
				body1.GetID().IsInvalid() ? 0 : 1,
				body1.IsDynamic() ? 1 : 0,
				pos1.GetX() * 100.0f, pos1.GetY() * 100.0f, pos1.GetZ() * 100.0f);
			UE_LOG(LogTemp, Warning, TEXT("  Body2: valid=%d, dynamic=%d, pos=(%.1f, %.1f, %.1f)"),
				body2.GetID().IsInvalid() ? 0 : 1,
				body2.IsDynamic() ? 1 : 0,
				pos2.GetX() * 100.0f, pos2.GetY() * 100.0f, pos2.GetZ() * 100.0f);
			UE_LOG(LogTemp, Warning, TEXT("  Actual distance: %.2f m (%.1f cm), Limits: [%.2f, %.2f] m"),
				actualDist, actualDist * 100.0f, settings.mMinDistance, settings.mMaxDistance);
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
