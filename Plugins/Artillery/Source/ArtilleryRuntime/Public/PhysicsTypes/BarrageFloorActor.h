// Simple Blueprint-friendly actor for creating Barrage floor physics
// Just drag and drop into level!

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkeletonTypes.h"
#include "BarrageFloorActor.generated.h"

/**
 * Simple actor that creates Barrage floor physics on BeginPlay
 *
 * USAGE:
 * 1. Drag BarrageFloorActor into your level
 * 2. Position it where you want the floor
 * 3. Adjust FloorSize in Details panel
 * 4. Play!
 *
 * The floor is invisible but has full Barrage physics collision.
 * All Barrage objects (created with BarrageEntitySpawner or Blueprint Library) will collide with it.
 */
UCLASS(Blueprintable, BlueprintType)
class ARTILLERYRUNTIME_API ABarrageFloorActor : public AActor
{
	GENERATED_BODY()

public:
	ABarrageFloorActor();

	/** Size of the floor box (X = Width, Y = Length, Z = Thickness) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Floor")
	FVector FloorSize = FVector(20000.0, 20000.0, 100.0);

	/** Offset from actor location (use negative Z to place floor below actor) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Floor")
	FVector FloorOffset = FVector(0, 0, -50);

	/** Show debug visualization in editor? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Floor")
	bool bShowDebugBox = true;

	/** Get the entity key of the floor (valid after BeginPlay) */
	UFUNCTION(BlueprintPure, Category = "Barrage Floor")
	FSkeletonKey GetFloorKey() const { return FloorKey; }

protected:
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
#endif

private:
	FSkeletonKey FloorKey;

	UPROPERTY()
	UBillboardComponent* SpriteComponent;
};
