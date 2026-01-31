// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FBarrageKey.h"
#include "FBConstraintParams.generated.h"

// Forward declaration
namespace JPH { class Constraint; template<class T> class Ref; }

/**
 * Unique identifier for a constraint in Barrage.
 * Similar to FBarrageKey but for constraints instead of bodies.
 */
USTRUCT(BlueprintType)
struct BARRAGE_API FBarrageConstraintKey
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Constraint")
	int64 Key = 0;

	FBarrageConstraintKey() : Key(0) {}
	FBarrageConstraintKey(uint64 InKey) : Key(static_cast<int64>(InKey)) {}

	bool IsValid() const { return Key != 0; }

	friend uint64 GetTypeHash(const FBarrageConstraintKey& Other)
	{
		return static_cast<uint64>(Other.Key);
	}

	bool operator==(const FBarrageConstraintKey& Other) const
	{
		return Key == Other.Key;
	}
};

/**
 * Constraint space - where anchor points and axes are defined.
 */
UENUM(BlueprintType)
enum class EBConstraintSpace : uint8
{
	/** Anchor points are in world space */
	WorldSpace,
	/** Anchor points are local to each body's center of mass */
	LocalToBody
};

/**
 * Types of constraints available in Barrage.
 */
UENUM(BlueprintType)
enum class EBConstraintType : uint8
{
	/** Fixed constraint - welds two bodies together (0 degrees of freedom) */
	Fixed,
	/** Point constraint - bodies connected at a point, can rotate freely (ball joint) */
	Point,
	/** Hinge constraint - rotation around a single axis (door hinge) */
	Hinge,
	/** Slider constraint - translation along a single axis (piston) */
	Slider,
	/** Distance constraint - maintains distance between anchor points (rope/spring) */
	Distance,
	/** Cone constraint - rotation limited to a cone */
	Cone,
	/** SixDOF constraint - full control over all 6 degrees of freedom */
	SixDOF
};

/**
 * Base parameters shared by all constraint types.
 */
struct FBConstraintParamsBase
{
	/** First body in the constraint */
	FBarrageKey Body1;

	/** Second body in the constraint. If invalid, Body1 is constrained to world. */
	FBarrageKey Body2;

	/** Coordinate space for anchor points and axes */
	EBConstraintSpace Space = EBConstraintSpace::WorldSpace;

	/**
	 * If true and Space is WorldSpace, anchor points are auto-calculated
	 * from current body positions (bodies frozen in place relative to each other)
	 */
	bool bAutoDetectAnchor = true;

	/** Anchor point on Body1 (world or local space depending on Space) */
	FVector3d AnchorPoint1 = FVector3d::ZeroVector;

	/** Anchor point on Body2 (world or local space depending on Space) */
	FVector3d AnchorPoint2 = FVector3d::ZeroVector;

	/**
	 * Break force threshold in Newtons. If constraint force exceeds this,
	 * the constraint breaks. Set to 0 for unbreakable.
	 */
	float BreakForce = 0.0f;

	/**
	 * Break torque threshold in Newton-meters. If constraint torque exceeds this,
	 * the constraint breaks. Set to 0 for unbreakable.
	 */
	float BreakTorque = 0.0f;

	/** User data for game logic */
	uint64 UserData = 0;
};

/**
 * Parameters for Fixed Constraint.
 * Welds two bodies together - no relative movement allowed.
 * Good for: destructible connections, compound objects that can break apart.
 */
struct FBFixedConstraintParams : public FBConstraintParamsBase
{
	// Fixed constraint has no additional parameters beyond base
};

/**
 * Parameters for Point Constraint (Ball Joint).
 * Bodies connected at a point but can rotate freely around it.
 * Good for: ragdoll joints, chain links, pendulums.
 */
struct FBPointConstraintParams : public FBConstraintParamsBase
{
	// Point constraint has no additional parameters beyond base
};

/**
 * Parameters for Hinge Constraint.
 * Rotation allowed around a single axis only.
 * Good for: doors, wheels, flaps, any rotating mechanism.
 */
struct FBHingeConstraintParams : public FBConstraintParamsBase
{
	/** Hinge axis direction (in coordinate space defined by Space) */
	FVector3d HingeAxis = FVector3d(0, 0, 1);

	/** Normal axis perpendicular to hinge (used for angle measurement) */
	FVector3d NormalAxis = FVector3d(1, 0, 0);

	/** Enable angle limits */
	bool bHasLimits = false;

	/** Minimum angle in radians (when bHasLimits is true) */
	float MinAngle = -PI;

	/** Maximum angle in radians (when bHasLimits is true) */
	float MaxAngle = PI;

	/** Enable motor */
	bool bEnableMotor = false;

	/** Target angular velocity for motor (radians/second) */
	float MotorTargetVelocity = 0.0f;

	/** Maximum torque the motor can apply */
	float MotorMaxTorque = 0.0f;
};

/**
 * Parameters for Slider Constraint.
 * Translation allowed along a single axis only.
 * Good for: pistons, sliding doors, elevator platforms.
 */
struct FBSliderConstraintParams : public FBConstraintParamsBase
{
	/** Slider axis direction (in coordinate space defined by Space) */
	FVector3d SliderAxis = FVector3d(1, 0, 0);

	/** Normal axis perpendicular to slider */
	FVector3d NormalAxis = FVector3d(0, 1, 0);

	/** Enable position limits */
	bool bHasLimits = false;

	/** Minimum position along slider axis (when bHasLimits is true) */
	float MinLimit = -100.0f;

	/** Maximum position along slider axis (when bHasLimits is true) */
	float MaxLimit = 100.0f;

	/** Enable motor */
	bool bEnableMotor = false;

	/** Target velocity for motor (units/second) */
	float MotorTargetVelocity = 0.0f;

	/** Maximum force the motor can apply */
	float MotorMaxForce = 0.0f;

	/**
	 * Spring frequency in Hz. Set to 0 for hard constraint.
	 * Higher values = stiffer spring.
	 */
	float SpringFrequency = 0.0f;

	/**
	 * Spring damping ratio. 0 = no damping, 1 = critical damping.
	 * Only used when SpringFrequency > 0.
	 */
	float SpringDamping = 0.0f;
};

/**
 * Parameters for Distance Constraint.
 * Maintains distance between two anchor points.
 * Good for: ropes, chains, springs, bungee cords.
 */
struct FBDistanceConstraintParams : public FBConstraintParamsBase
{
	/** Minimum distance allowed (0 = no minimum) */
	float MinDistance = 0.0f;

	/** Maximum distance allowed (0 = auto-detect from initial positions) */
	float MaxDistance = 0.0f;

	/**
	 * Spring frequency in Hz. Set to 0 for hard constraint.
	 * Higher values = stiffer spring.
	 */
	float SpringFrequency = 0.0f;

	/**
	 * Spring damping ratio. 0 = no damping, 1 = critical damping.
	 * Only used when SpringFrequency > 0.
	 */
	float SpringDamping = 0.0f;

	/**
	 * Lock relative rotation between bodies.
	 * When true, uses SliderConstraint internally instead of DistanceConstraint.
	 * Bodies maintain their relative orientation but can compress/extend like a telescoping rod.
	 */
	bool bLockRotation = false;
};

/**
 * Parameters for Cone Constraint.
 * Rotation limited to a cone around an axis.
 * Good for: shoulder joints, lamp arm joints.
 */
struct FBConeConstraintParams : public FBConstraintParamsBase
{
	/** Cone axis direction (in coordinate space defined by Space) */
	FVector3d ConeAxis = FVector3d(0, 0, 1);

	/** Half angle of the cone in radians */
	float HalfConeAngle = PI / 4.0f;
};

/**
 * Result of a constraint query - provides impulse/force info for break detection.
 */
struct FBConstraintForces
{
	/** Linear force applied by the constraint (Newtons) */
	FVector3d LinearForce = FVector3d::ZeroVector;

	/** Angular torque applied by the constraint (Newton-meters) */
	FVector3d AngularTorque = FVector3d::ZeroVector;

	/** Magnitude of linear force */
	float GetForceMagnitude() const { return LinearForce.Length(); }

	/** Magnitude of angular torque */
	float GetTorqueMagnitude() const { return AngularTorque.Length(); }
};
