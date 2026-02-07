// Blueprint function library for Flecs ECS constraint operations.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SkeletonTypes.h"
#include "FlecsConstraintLibrary.generated.h"

UCLASS()
class UFlecsConstraintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// CONSTRAINTS (game-thread safe, enqueued to simulation thread)
	// ═══════════════════════════════════════════════════════════════

	UFUNCTION(BlueprintCallable, Category = "Flecs|Constraints", meta = (WorldContext = "WorldContextObject"))
	static int64 CreateFixedConstraint(
		UObject* WorldContextObject,
		FSkeletonKey Entity1Key,
		FSkeletonKey Entity2Key,
		float BreakForce = 0.f,
		float BreakTorque = 0.f
	);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Constraints", meta = (WorldContext = "WorldContextObject"))
	static int64 CreateHingeConstraint(
		UObject* WorldContextObject,
		FSkeletonKey Entity1Key,
		FSkeletonKey Entity2Key,
		FVector WorldAnchor,
		FVector HingeAxis,
		float BreakForce = 0.f
	);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Constraints", meta = (WorldContext = "WorldContextObject"))
	static int64 CreateDistanceConstraint(
		UObject* WorldContextObject,
		FSkeletonKey Entity1Key,
		FSkeletonKey Entity2Key,
		float MinDistance = 0.f,
		float MaxDistance = 0.f,
		float BreakForce = 0.f,
		float SpringFrequency = 0.f,
		float SpringDamping = 0.5f,
		bool bLockRotation = false
	);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Constraints", meta = (WorldContext = "WorldContextObject"))
	static int64 CreatePointConstraint(
		UObject* WorldContextObject,
		FSkeletonKey Entity1Key,
		FSkeletonKey Entity2Key,
		float BreakForce = 0.f,
		float BreakTorque = 0.f
	);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Constraints", meta = (WorldContext = "WorldContextObject"))
	static bool RemoveConstraint(UObject* WorldContextObject, int64 ConstraintKey);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Constraints", meta = (WorldContext = "WorldContextObject"))
	static int32 RemoveAllConstraintsFromEntity(UObject* WorldContextObject, FSkeletonKey EntityKey);

	UFUNCTION(BlueprintPure, Category = "Flecs|Constraints", meta = (WorldContext = "WorldContextObject"))
	static bool IsConstraintActive(UObject* WorldContextObject, int64 ConstraintKey);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Constraints", meta = (WorldContext = "WorldContextObject"))
	static bool GetConstraintStressRatio(UObject* WorldContextObject, int64 ConstraintKey, float& OutStressRatio);
};
