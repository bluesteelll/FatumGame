// Data Asset for spawning groups of constrained Flecs entities.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EPhysicsLayer.h"
#include "SkeletonTypes.h"
#include "FlecsConstrainedGroupDefinition.generated.h"

class UStaticMesh;
class UMaterialInterface;

/**
 * Type of constraint between elements.
 */
UENUM(BlueprintType)
enum class EFlecsConstraintType : uint8
{
	/** Fixed (welded) - no relative movement */
	Fixed,
	/** Hinge - rotation around axis */
	Hinge,
	/** Distance - rope/spring */
	Distance,
	/** Point - ball joint, free rotation */
	Point
};

/**
 * Single element in a constrained group.
 */
USTRUCT(BlueprintType)
struct FFlecsGroupElement
{
	GENERATED_BODY()

	/** Display name for editor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element")
	FName ElementName = "Element";

	/** Static mesh for this element */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element")
	TObjectPtr<UStaticMesh> Mesh;

	/** Optional material override */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element")
	TObjectPtr<UMaterialInterface> Material;

	/** Local offset from group origin */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FVector LocalOffset = FVector::ZeroVector;

	/** Local rotation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FRotator LocalRotation = FRotator::ZeroRotator;

	/** Scale */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FVector Scale = FVector::OneVector;

	/** Physics layer */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	EPhysicsLayer PhysicsLayer = EPhysicsLayer::MOVING;

	/** Is this element movable? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	bool bIsMovable = true;

	/** Surface friction. 0 = ice, 1 = sticky rubber */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Friction = 0.5f;

	/** Bounciness. 0 = no bounce, 1 = full energy return */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Restitution = 0.3f;

	/** Max health (0 = indestructible) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay", meta = (ClampMin = "0"))
	float MaxHealth = 0.f;

	/** Armor (damage reduction) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay", meta = (ClampMin = "0"))
	float Armor = 0.f;
};

/**
 * Constraint definition between two elements.
 */
USTRUCT(BlueprintType)
struct FFlecsGroupConstraint
{
	GENERATED_BODY()

	/** Index of first element (0-based) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint", meta = (ClampMin = "0"))
	int32 Element1Index = 0;

	/** Index of second element (0-based) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint", meta = (ClampMin = "0"))
	int32 Element2Index = 1;

	/** Type of constraint */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint")
	EFlecsConstraintType ConstraintType = EFlecsConstraintType::Fixed;

	/**
	 * Breaking force in Newtons. Connection breaks when force exceeds this.
	 * 0 = unbreakable. Typical values: 1000-10000 for breakable, 0 for permanent.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking", meta = (ClampMin = "0", UIMin = "0", UIMax = "50000"))
	float BreakForce = 0.f;

	/**
	 * Breaking torque in Nm. Connection breaks when rotation force exceeds this.
	 * 0 = unbreakable. Only applies to Fixed/Point constraints.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breaking", meta = (ClampMin = "0", UIMin = "0", UIMax = "50000"))
	float BreakTorque = 0.f;

	// === Anchor Points (for Hinge/Distance) ===

	/** Local anchor offset from Element1 center */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchors")
	FVector AnchorOffset1 = FVector::ZeroVector;

	/** Local anchor offset from Element2 center */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchors")
	FVector AnchorOffset2 = FVector::ZeroVector;

	// === Hinge-specific ===

	/** Hinge rotation axis (local to Element1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hinge")
	FVector HingeAxis = FVector(0, 0, 1);

	// === Distance/Rope Settings ===

	/**
	 * Minimum rope length in CENTIMETERS. Bodies cannot get closer than this.
	 * 0 = no minimum (rope can go slack).
	 * Example: 100 = 1 meter, 500 = 5 meters
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance", meta = (ClampMin = "0", UIMin = "0", UIMax = "10000"))
	float MinDistance = 0.f;

	/**
	 * Maximum rope length in CENTIMETERS. Bodies cannot get further than this.
	 * 0 = auto-calculate from initial element positions.
	 * Example: 100 = 1 meter, 500 = 5 meters
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance", meta = (ClampMin = "0", UIMin = "0", UIMax = "10000"))
	float MaxDistance = 0.f;

	/**
	 * Spring stiffness in Hz. Higher = stiffer spring.
	 * 0 = rigid rope (no stretch). 1-5 = soft spring. 10+ = stiff spring.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance", meta = (ClampMin = "0", UIMin = "0", UIMax = "20"))
	float SpringFrequency = 0.f;

	/**
	 * Spring damping ratio. Controls bounce.
	 * 0 = bounces forever. 0.5 = some bounce. 1 = no bounce (critically damped).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float SpringDamping = 0.5f;

	// === Rotation Lock ===

	/**
	 * Lock relative rotation between bodies.
	 * When true, bodies cannot rotate independently - they maintain fixed orientation relative to each other.
	 * Works with Distance and Point constraints. Fixed already locks rotation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation")
	bool bLockRotation = false;
};

/**
 * Data Asset defining a group of constrained Flecs entities.
 * Create in Content Browser: Right Click -> Miscellaneous -> Data Asset -> FlecsConstrainedGroupDefinition
 *
 * Use cases:
 * - Chain of linked boxes
 * - Destructible bridge segments
 * - Hinged doors/gates
 * - Rope bridges
 * - Compound destructible objects
 *
 * Usage:
 * 1. Add Elements (each element becomes a physics body)
 * 2. Add Constraints (link elements together)
 * 3. Call SpawnConstrainedGroup() from Blueprint/C++
 */
UCLASS(BlueprintType)
class FATUMGAME_API UFlecsConstrainedGroupDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// IDENTIFICATION
	// ═══════════════════════════════════════════════════════════════

	/** Unique name for this group type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Group")
	FName GroupName = "DefaultGroup";

	// ═══════════════════════════════════════════════════════════════
	// ELEMENTS
	// ═══════════════════════════════════════════════════════════════

	/** Elements in this group (each becomes a physics body with Flecs entity) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elements")
	TArray<FFlecsGroupElement> Elements;

	// ═══════════════════════════════════════════════════════════════
	// CONSTRAINTS
	// ═══════════════════════════════════════════════════════════════

	/** Constraints between elements */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraints")
	TArray<FFlecsGroupConstraint> Constraints;

	// ═══════════════════════════════════════════════════════════════
	// PRESETS
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Generate a chain of N identical elements connected by constraints.
	 * Call this from editor context menu or Blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "Presets")
	void GenerateChain(UStaticMesh* Mesh, int32 Count, FVector Spacing, EFlecsConstraintType LinkType = EFlecsConstraintType::Fixed, float BreakForce = 0.f);

	/**
	 * Generate a grid of elements (rows x columns) connected horizontally and vertically.
	 */
	UFUNCTION(BlueprintCallable, Category = "Presets")
	void GenerateGrid(UStaticMesh* Mesh, int32 Rows, int32 Columns, FVector Spacing, EFlecsConstraintType LinkType = EFlecsConstraintType::Fixed, float BreakForce = 0.f);

	// ═══════════════════════════════════════════════════════════════
	// VALIDATION
	// ═══════════════════════════════════════════════════════════════

	/** Check if all constraint indices are valid */
	UFUNCTION(BlueprintPure, Category = "Validation")
	bool IsValid() const;

	/** Get number of elements */
	UFUNCTION(BlueprintPure, Category = "Info")
	int32 GetElementCount() const { return Elements.Num(); }

	/** Get number of constraints */
	UFUNCTION(BlueprintPure, Category = "Info")
	int32 GetConstraintCount() const { return Constraints.Num(); }

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId("FlecsConstrainedGroupDefinition", GroupName);
	}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

/**
 * Result of spawning a constrained group.
 */
USTRUCT(BlueprintType)
struct FFlecsGroupSpawnResult
{
	GENERATED_BODY()

	/** Was spawn successful? */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bSuccess = false;

	/** SkeletonKeys of all spawned elements (same order as Definition.Elements) */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TArray<FSkeletonKey> ElementKeys;

	/** Constraint keys for all created constraints (same order as Definition.Constraints) */
	UPROPERTY(BlueprintReadOnly, Category = "Result")
	TArray<int64> ConstraintKeys;
};
