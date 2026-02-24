// Door profile for Flecs entity spawning.
// Defines hinge/slide configuration, motor tuning, and door behavior.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsDoorProfile.generated.h"

UENUM(BlueprintType)
enum class EFlecsDoorType : uint8
{
	Hinged  UMETA(DisplayName = "Hinged"),
	Sliding UMETA(DisplayName = "Sliding")
};

/**
 * Data Asset defining door properties for entity spawning.
 *
 * Used with FEntitySpawnRequest to make an entity a physics-driven door.
 * Supports hinged doors (rotate around axis) and sliding doors (translate along axis).
 * Motor-driven movement via Jolt constraint motors.
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsDoorProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// DOOR TYPE
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	EFlecsDoorType DoorType = EFlecsDoorType::Hinged;

	// ═══════════════════════════════════════════════════════════════
	// HINGED DOOR
	// ═══════════════════════════════════════════════════════════════

	/** Maximum open angle in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hinged",
		meta = (EditCondition = "bIsHinged", EditConditionHides, ClampMin = "10", ClampMax = "170"))
	float MaxOpenAngleDegrees = 90.f;

	/** Hinge rotation axis (UE coordinates, typically Z-up) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hinged",
		meta = (EditCondition = "bIsHinged", EditConditionHides))
	FVector HingeAxis = FVector(0, 0, 1);

	/** Offset from body center to hinge pivot (cm, local space) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hinged",
		meta = (EditCondition = "bIsHinged", EditConditionHides))
	FVector HingeOffset = FVector::ZeroVector;

	/** Can open in both directions based on player approach side */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hinged",
		meta = (EditCondition = "bIsHinged", EditConditionHides))
	bool bBidirectional = true;

	// ═══════════════════════════════════════════════════════════════
	// SLIDING DOOR
	// ═══════════════════════════════════════════════════════════════

	/** Slide direction in local space (normalized) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sliding",
		meta = (EditCondition = "bIsSliding", EditConditionHides))
	FVector SlideDirection = FVector(1, 0, 0);

	/** Slide distance in cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sliding",
		meta = (EditCondition = "bIsSliding", EditConditionHides, ClampMin = "10"))
	float SlideDistanceCm = 200.f;

	// ═══════════════════════════════════════════════════════════════
	// MOTOR
	// ═══════════════════════════════════════════════════════════════

	/** Use motor-driven movement (vs. impulse-based) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motor")
	bool bMotorDriven = true;

	/** Motor spring frequency (Hz) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motor",
		meta = (ClampMin = "0.5", ClampMax = "20"))
	float MotorFrequency = 4.f;

	/** Motor damping ratio (1.0 = critical damping) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motor",
		meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float MotorDamping = 1.0f;

	/** Max torque N*m (hinge) or force N (slider) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motor",
		meta = (ClampMin = "10"))
	float MotorMaxForce = 500.f;

	/** Friction when motor off (resistance to push, anti-jitter) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motor",
		meta = (ClampMin = "0"))
	float FrictionForce = 5.f;

	// ═══════════════════════════════════════════════════════════════
	// BEHAVIOR
	// ═══════════════════════════════════════════════════════════════

	/** Automatically close after delay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bAutoClose = false;

	/** Seconds before auto-closing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior",
		meta = (EditCondition = "bAutoClose", ClampMin = "0.1"))
	float AutoCloseDelay = 3.f;

	/** Door starts locked (requires trigger/key to unlock) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bStartsLocked = false;

	/** Player can unlock by pressing E. If false, only external trigger can unlock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior",
		meta = (EditCondition = "bStartsLocked"))
	bool bUnlockOnInteraction = true;

	/** Latch door at end positions with heavy mass.
	 *  Any force still moves the door, but very slowly. Sustained push overcomes inertia. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bLockAtEndPosition = false;

	/** Mass (kg) at end positions. Higher = harder to push off latch. Normal Mass restored when moving.
	 *  ~200: easy push. ~500: heavy. ~2000: nearly immovable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior",
		meta = (EditCondition = "bLockAtEndPosition", ClampMin = "50", ClampMax = "10000"))
	float LockMass = 500.f;

	// ═══════════════════════════════════════════════════════════════
	// PHYSICS
	// ═══════════════════════════════════════════════════════════════

	/** Door body mass (kg) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics",
		meta = (ClampMin = "1", ClampMax = "500"))
	float Mass = 25.f;

	/** Angular damping for the door body */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics",
		meta = (ClampMin = "0", ClampMax = "5"))
	float AngularDamping = 0.5f;

	/** Force (N) to break door off hinges. 0 = unbreakable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics",
		meta = (ClampMin = "0"))
	float ConstraintBreakForce = 0.f;

	/** Torque (Nm) to break door off hinges. 0 = unbreakable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics",
		meta = (ClampMin = "0"))
	float ConstraintBreakTorque = 0.f;

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	float GetMaxOpenAngleRadians() const { return FMath::DegreesToRadians(MaxOpenAngleDegrees); }
	float GetSlideDistanceMeters() const { return SlideDistanceCm / 100.f; }
	bool IsHinged() const { return DoorType == EFlecsDoorType::Hinged; }
	bool IsSliding() const { return DoorType == EFlecsDoorType::Sliding; }

#if WITH_EDITORONLY_DATA
private:
	// Helper bools for EditCondition (enum == comparison is unreliable in UE meta).
	// NOT serialized -- computed from DoorType via PostEditChangeProperty.

	UPROPERTY(Transient, meta = (HideInDetailPanel))
	bool bIsHinged = true;

	UPROPERTY(Transient, meta = (HideInDetailPanel))
	bool bIsSliding = false;

public:
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;

private:
	void RefreshEditorBools();
#endif
};
