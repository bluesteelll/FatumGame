
#pragma once

#include "CoreMinimal.h"
#include "FBConstraintParams.h"
#include "IsolatedJoltIncludes.h"

// Forward declarations
class FWorldSimOwner;

/**
 * High-level constraint management system for Barrage physics.
 *
 * Provides an easy-to-use API for creating physics constraints between bodies,
 * with support for breakable connections and runtime force queries.
 *
 * Usage Example:
 * @code
 *     // Get the constraint system from your world sim owner
 *     auto& Constraints = WorldSim->GetConstraintSystem();
 *
 *     // Create a fixed (welded) connection between two bodies
 *     FBFixedConstraintParams Params;
 *     Params.Body1 = BodyKeyA;
 *     Params.Body2 = BodyKeyB;
 *     Params.BreakForce = 10000.0f;  // Breaks at 10kN
 *
 *     FBarrageConstraintKey ConstraintKey = Constraints.CreateFixed(Params);
 *
 *     // Later, check if it should break
 *     if (Constraints.ShouldBreak(ConstraintKey))
 *     {
 *         Constraints.Remove(ConstraintKey);
 *     }
 * @endcode
 */
class BARRAGE_API FBarrageConstraintSystem
{
public:
	FBarrageConstraintSystem(FWorldSimOwner* InOwner);
	~FBarrageConstraintSystem();

	// ============================================================
	// Constraint Creation
	// ============================================================

	/**
	 * Create a fixed constraint that welds two bodies together.
	 * No relative movement is allowed between the bodies.
	 *
	 * @param Params Constraint parameters
	 * @return Unique key for the created constraint, or invalid key on failure
	 */
	FBarrageConstraintKey CreateFixed(const FBFixedConstraintParams& Params);

	/**
	 * Create a point constraint (ball joint) between two bodies.
	 * Bodies can rotate freely around the connection point.
	 *
	 * @param Params Constraint parameters
	 * @return Unique key for the created constraint
	 */
	FBarrageConstraintKey CreatePoint(const FBPointConstraintParams& Params);

	/**
	 * Create a hinge constraint between two bodies.
	 * Bodies can only rotate around the specified axis.
	 *
	 * @param Params Constraint parameters including axis and optional limits
	 * @return Unique key for the created constraint
	 */
	FBarrageConstraintKey CreateHinge(const FBHingeConstraintParams& Params);

	/**
	 * Create a slider constraint between two bodies.
	 * Bodies can only translate along the specified axis.
	 *
	 * @param Params Constraint parameters including axis and optional limits
	 * @return Unique key for the created constraint
	 */
	FBarrageConstraintKey CreateSlider(const FBSliderConstraintParams& Params);

	/**
	 * Create a distance constraint between two bodies.
	 * Maintains a specific distance (or range) between anchor points.
	 * Can act as a rope, chain, or spring.
	 *
	 * @param Params Constraint parameters including distance limits and spring settings
	 * @return Unique key for the created constraint
	 */
	FBarrageConstraintKey CreateDistance(const FBDistanceConstraintParams& Params);

	/**
	 * Create a cone constraint between two bodies.
	 * Rotation is limited to within a cone around the specified axis.
	 *
	 * @param Params Constraint parameters including cone axis and angle
	 * @return Unique key for the created constraint
	 */
	FBarrageConstraintKey CreateCone(const FBConeConstraintParams& Params);

	// ============================================================
	// Constraint Management
	// ============================================================

	/**
	 * Remove a constraint from the simulation.
	 * The bodies will no longer be connected.
	 *
	 * @param Key The constraint to remove
	 * @return True if the constraint was found and removed
	 */
	bool Remove(FBarrageConstraintKey Key);

	/**
	 * Remove all constraints connected to a specific body.
	 * Useful when destroying a body.
	 *
	 * @param BodyKey The body whose constraints should be removed
	 * @return Number of constraints removed
	 */
	int32 RemoveAllForBody(FBarrageKey BodyKey);

	/**
	 * Enable or disable a constraint without removing it.
	 * Disabled constraints don't affect the simulation but can be re-enabled.
	 *
	 * @param Key The constraint to enable/disable
	 * @param bEnabled True to enable, false to disable
	 */
	void SetEnabled(FBarrageConstraintKey Key, bool bEnabled);

	/**
	 * Check if a constraint is currently enabled.
	 *
	 * @param Key The constraint to check
	 * @return True if enabled, false if disabled or not found
	 */
	bool IsEnabled(FBarrageConstraintKey Key) const;

	/**
	 * Check if a constraint key is valid and the constraint still exists.
	 *
	 * @param Key The constraint to check
	 * @return True if the constraint exists
	 */
	bool IsValid(FBarrageConstraintKey Key) const;

	// ============================================================
	// Force Queries & Breaking
	// ============================================================

	/**
	 * Get the forces currently acting on a constraint.
	 * Useful for determining if a constraint is under stress.
	 *
	 * @param Key The constraint to query
	 * @param OutForces Receives the force information
	 * @return True if the constraint was found and forces retrieved
	 */
	bool GetForces(FBarrageConstraintKey Key, FBConstraintForces& OutForces) const;

	/**
	 * Check if a constraint should break based on its break thresholds.
	 * Compares current forces against BreakForce and BreakTorque.
	 *
	 * @param Key The constraint to check
	 * @return True if either force or torque exceeds the break threshold
	 */
	bool ShouldBreak(FBarrageConstraintKey Key) const;

	/**
	 * Process all constraints and break any that exceed their thresholds.
	 * Call this once per physics tick if using breakable constraints.
	 *
	 * @param OutBrokenConstraints Optional array to receive keys of broken constraints
	 * @return Number of constraints that were broken
	 */
	int32 ProcessBreakableConstraints(TArray<FBarrageConstraintKey>* OutBrokenConstraints = nullptr);

	// ============================================================
	// Bulk Operations
	// ============================================================

	/**
	 * Get all constraint keys in the system.
	 *
	 * @param OutKeys Array to receive all constraint keys
	 */
	void GetAllConstraints(TArray<FBarrageConstraintKey>& OutKeys) const;

	/**
	 * Get all constraints connected to a specific body.
	 *
	 * @param BodyKey The body to query
	 * @param OutKeys Array to receive constraint keys
	 */
	void GetConstraintsForBody(FBarrageKey BodyKey, TArray<FBarrageConstraintKey>& OutKeys) const;

	/**
	 * Get the total number of active constraints.
	 */
	int32 GetConstraintCount() const;

	/**
	 * Remove all constraints from the system.
	 */
	void Clear();

private:
	/** Owning physics world */
	FWorldSimOwner* Owner;

	/** Internal constraint data with break thresholds */
	struct FConstraintData
	{
		JPH::Ref<JPH::Constraint> JoltConstraint;
		FBarrageKey Body1;
		FBarrageKey Body2;
		float BreakForce;
		float BreakTorque;
		uint64 UserData;
	};

	/** Map from our keys to constraint data */
	TMap<FBarrageConstraintKey, FConstraintData> Constraints;

	/** Reverse lookup: body key -> constraint keys */
	TMultiMap<FBarrageKey, FBarrageConstraintKey> BodyToConstraints;

	/** Next available constraint key */
	uint64 NextConstraintKey = 1;

	/** Generate a new unique constraint key */
	FBarrageConstraintKey GenerateKey();

	/** Register a constraint in our tracking maps */
	void RegisterConstraint(FBarrageConstraintKey Key, const FConstraintData& Data);

	/** Internal helper to get Jolt body references */
	bool GetJoltBodies(FBarrageKey Key1, FBarrageKey Key2,
					   JPH::Body*& OutBody1, JPH::Body*& OutBody2) const;
};
