
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FBConstraintParams.h"
#include "BarrageBodyOwner.h"
#include "BarrageConstraintComponent.generated.h"

class UBarrageDispatch;

/**
 * Blueprint-accessible delegate for constraint break events.
 * Named FOnBarrageConstraintBroken to avoid conflict with FOnConstraintBroken in EngineTypes.h
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBarrageConstraintBroken, int32, ConstraintIndex);

/**
 * Serializable constraint definition for use in Blueprints.
 */
USTRUCT(BlueprintType)
struct BARRAGE_API FBarrageConstraintDefinition
{
	GENERATED_BODY()

	/** Type of constraint */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint")
	EBConstraintType Type = EBConstraintType::Fixed;

	/** Offset from this actor's origin to the constraint anchor point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint")
	FVector LocalAnchorOffset = FVector::ZeroVector;

	/** Target actor to connect to (if null, connects to world) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint")
	TWeakObjectPtr<AActor> TargetActor;

	/** Offset from target actor's origin to the constraint anchor point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint")
	FVector TargetAnchorOffset = FVector::ZeroVector;

	/** Force at which this constraint breaks (Newtons). 0 = unbreakable */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking", meta = (ClampMin = "0"))
	float BreakForce = 0.0f;

	/** Torque at which this constraint breaks (Newton-meters). 0 = unbreakable */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking", meta = (ClampMin = "0"))
	float BreakTorque = 0.0f;

	// === Hinge-specific ===

	/** Axis of rotation for hinge constraints */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hinge", meta = (EditCondition = "Type == EBConstraintType::Hinge"))
	FVector HingeAxis = FVector(0, 0, 1);

	/** Enable angle limits for hinge */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hinge", meta = (EditCondition = "Type == EBConstraintType::Hinge"))
	bool bHingeLimits = false;

	/** Minimum angle in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hinge", meta = (EditCondition = "Type == EBConstraintType::Hinge && bHingeLimits"))
	float HingeMinAngle = -180.0f;

	/** Maximum angle in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hinge", meta = (EditCondition = "Type == EBConstraintType::Hinge && bHingeLimits"))
	float HingeMaxAngle = 180.0f;

	// === Distance-specific ===

	/** Minimum distance for distance constraint */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance", meta = (EditCondition = "Type == EBConstraintType::Distance", ClampMin = "0"))
	float MinDistance = 0.0f;

	/** Maximum distance for distance constraint (0 = auto-detect from initial position) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance", meta = (EditCondition = "Type == EBConstraintType::Distance", ClampMin = "0"))
	float MaxDistance = 0.0f;

	/** Spring frequency (0 = rigid constraint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance", meta = (EditCondition = "Type == EBConstraintType::Distance", ClampMin = "0"))
	float SpringFrequency = 0.0f;

	/** Spring damping ratio (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance", meta = (EditCondition = "Type == EBConstraintType::Distance", ClampMin = "0", ClampMax = "1"))
	float SpringDamping = 0.5f;

	// === Runtime state (not serialized) ===

	/** Internal key - not for Blueprint use */
	FBarrageConstraintKey RuntimeKey;

	/** Whether this constraint has been created */
	bool bCreated = false;
};

/**
 * Component for managing Barrage physics constraints on an actor.
 *
 * This component provides a Blueprint-friendly interface for creating
 * breakable connections between physics bodies. Constraints can be
 * defined in the editor and are automatically created when the owning
 * actor's physics body is registered with Barrage.
 *
 * Usage:
 * 1. Add this component to an actor with a Barrage physics body
 * 2. Define constraints in the Constraints array
 * 3. Constraints are automatically created when physics is initialized
 * 4. Listen to OnConstraintBroken for break events
 *
 * For manual control, use CreateConstraint/RemoveConstraint at runtime.
 */
UCLASS(ClassGroup=(Physics), meta=(BlueprintSpawnableComponent))
class BARRAGE_API UBarrageConstraintComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBarrageConstraintComponent();

	// === Properties ===

	/** Constraints to create when this actor's physics body is initialized */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraints")
	TArray<FBarrageConstraintDefinition> Constraints;

	/** Automatically check for and process broken constraints each tick */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraints")
	bool bAutoProcessBreaking = true;

	/** Called when a constraint breaks due to exceeding force/torque thresholds */
	UPROPERTY(BlueprintAssignable, Category = "Constraints|Events")
	FOnBarrageConstraintBroken OnConstraintBroken;

	// === Blueprint Functions ===

	/**
	 * Create all defined constraints. Called automatically when physics body is ready.
	 * @return Number of constraints successfully created
	 */
	UFUNCTION(BlueprintCallable, Category = "Constraints")
	int32 CreateAllConstraints();

	/**
	 * Remove all constraints created by this component.
	 */
	UFUNCTION(BlueprintCallable, Category = "Constraints")
	void RemoveAllConstraints();

	/**
	 * Create a single constraint at runtime.
	 * @param Definition The constraint parameters
	 * @return Index of the created constraint, or -1 on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Constraints")
	int32 CreateConstraint(const FBarrageConstraintDefinition& Definition);

	/**
	 * Remove a specific constraint by index.
	 * @param Index The constraint index to remove
	 * @return True if the constraint was found and removed
	 */
	UFUNCTION(BlueprintCallable, Category = "Constraints")
	bool RemoveConstraint(int32 Index);

	/**
	 * Check if a specific constraint is still active (not broken).
	 * @param Index The constraint index to check
	 * @return True if the constraint exists and is active
	 */
	UFUNCTION(BlueprintCallable, Category = "Constraints")
	bool IsConstraintActive(int32 Index) const;

	/**
	 * Get the current force on a constraint (useful for UI or pre-break warnings).
	 * @param Index The constraint index
	 * @param OutForce Receives the current force magnitude in Newtons
	 * @return True if the constraint exists
	 */
	UFUNCTION(BlueprintCallable, Category = "Constraints")
	bool GetConstraintForce(int32 Index, float& OutForce) const;

	/**
	 * Get the ratio of current force to break force (0-1+, >1 means should break).
	 * @param Index The constraint index
	 * @param OutRatio Receives the force ratio (0 = no stress, 1 = at break threshold)
	 * @return True if the constraint exists and has a break threshold
	 */
	UFUNCTION(BlueprintCallable, Category = "Constraints")
	bool GetConstraintStressRatio(int32 Index, float& OutRatio) const;

	/**
	 * Manually break a constraint (useful for gameplay events).
	 * @param Index The constraint index to break
	 * @return True if the constraint was found and broken
	 */
	UFUNCTION(BlueprintCallable, Category = "Constraints")
	bool BreakConstraint(int32 Index);

	/**
	 * Get the number of active constraints.
	 */
	UFUNCTION(BlueprintCallable, Category = "Constraints")
	int32 GetActiveConstraintCount() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Cached reference to Barrage dispatch */
	UPROPERTY()
	TWeakObjectPtr<UBarrageDispatch> CachedDispatch;

	/** Our actor's Barrage body key */
	FBarrageKey OwnerBodyKey;

	/** Check for broken constraints and fire events */
	void ProcessBreaking();

	/** Get the Barrage dispatch subsystem */
	class FBarrageConstraintSystem* GetConstraintSystem() const;

	/** Get the body key for an actor */
	FBarrageKey GetActorBodyKey(AActor* Actor) const;
};
