// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SkeletonTypes.h"
#include "BarrageBodyOwner.generated.h"

/**
 * Interface for actors that own a Barrage physics body.
 *
 * Implement this interface on any actor that has a physics body in Barrage
 * to allow the constraint system to automatically find and connect bodies.
 *
 * The interface uses FSkeletonKey (which is BlueprintType) and the constraint
 * component converts it to FBarrageKey internally via UBarrageDispatch.
 *
 * Usage:
 * 1. Have your actor class inherit from IBarrageBodyOwner
 * 2. Implement GetBodySkeletonKey() to return your actor's skeleton key
 * 3. Use UBarrageConstraintComponent to connect physics bodies in Blueprints
 *
 * Example:
 * @code
 *     class AMyActor : public AActor, public IBarrageBodyOwner
 *     {
 *         virtual FSkeletonKey GetBodySkeletonKey_Implementation() const override
 *         {
 *             return MyKeyCarry->MyObjectKey;
 *         }
 *     };
 * @endcode
 */
UINTERFACE(MinimalAPI, Blueprintable, BlueprintType)
class UBarrageBodyOwner : public UInterface
{
	GENERATED_BODY()
};

class BARRAGE_API IBarrageBodyOwner
{
	GENERATED_BODY()

public:
	/**
	 * Get the Skeleton key for this actor's physics body.
	 * The constraint system uses this to look up the FBarrageKey.
	 *
	 * @return The FSkeletonKey for this actor's physics body, or invalid key if none
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Barrage|Constraints")
	FSkeletonKey GetBodySkeletonKey() const;

	/**
	 * Check if this actor has a valid physics body in Barrage.
	 *
	 * @return True if the actor has a registered physics body
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Barrage|Constraints")
	bool HasBarrageBody() const;
};
