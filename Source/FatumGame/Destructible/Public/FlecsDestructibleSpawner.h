// Editor-placeable spawner for destructible objects.
// Validates EntityDefinition has a DestructibleProfile.
// Uses the unified UFlecsEntityLibrary::SpawnEntity API.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkeletonTypes.h"
#include "FlecsDestructibleSpawner.generated.h"

class UFlecsEntityDefinition;
class UStaticMeshComponent;

/**
 * Place in editor to spawn destructible Flecs entities at runtime.
 *
 * HOW TO USE:
 * 1. Drag to level
 * 2. Set EntityDefinition (must have DestructibleProfile)
 * 3. Play!
 *
 * The EntityDefinition must have:
 * - DestructibleProfile: fragment geometry, break force, etc.
 * - PhysicsProfile: static body (NON_MOVING layer recommended)
 * - RenderProfile: intact mesh
 *
 * Any other profiles (Health, Damage, Interaction, Container, etc.) are optional
 * and will be applied to the intact object.
 */
UCLASS(Blueprintable, BlueprintType)
class FATUMGAME_API AFlecsDestructibleSpawner : public AActor
{
	GENERATED_BODY()

public:
	AFlecsDestructibleSpawner();

	/** Entity definition. MUST have DestructibleProfile set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible")
	TObjectPtr<UFlecsEntityDefinition> EntityDefinition;

	/** Destroy this actor after spawning? (recommended) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible")
	bool bDestroyAfterSpawn = true;

	/** Spawn on BeginPlay? If false, call SpawnDestructible() manually. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible")
	bool bSpawnOnBeginPlay = true;

	/** Show mesh preview in editor? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	bool bShowPreview = true;

	/** Spawn the destructible entity. */
	UFUNCTION(BlueprintCallable, Category = "Destructible")
	FSkeletonKey SpawnDestructible();

	/** Get spawned entity key. */
	UFUNCTION(BlueprintPure, Category = "Destructible")
	FSkeletonKey GetEntityKey() const { return SpawnedEntityKey; }

	/** Check if already spawned. */
	UFUNCTION(BlueprintPure, Category = "Destructible")
	bool HasSpawned() const { return SpawnedEntityKey.IsValid(); }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UStaticMeshComponent> PreviewMeshComponent;

	FSkeletonKey SpawnedEntityKey;

#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void UpdatePreview();
#endif
};
