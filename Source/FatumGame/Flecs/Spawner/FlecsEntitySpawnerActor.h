// Flecs Entity Spawner Actor - place in editor, assign EntityDefinition, done!
// Uses unified UFlecsEntityLibrary::SpawnEntity API.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkeletonTypes.h"
#include "FlecsEntitySpawnerActor.generated.h"

class UFlecsEntityDefinition;
class UStaticMeshComponent;

/**
 * Place in editor to spawn Flecs entities at runtime.
 *
 * HOW TO USE:
 * 1. Drag to level
 * 2. Set EntityDefinition (Data Asset with all profiles)
 * 3. Play!
 *
 * The EntityDefinition controls everything:
 * - PhysicsProfile: collision, gravity, friction
 * - RenderProfile: mesh, material, scale
 * - HealthProfile: HP, armor, regen
 * - DamageProfile: contact damage
 * - ProjectileProfile: lifetime, bouncing
 * - ContainerProfile: inventory
 *
 * For simple physics-only objects without Flecs components,
 * use ABarrageEntitySpawner instead.
 */
UCLASS(Blueprintable, BlueprintType)
class FATUMGAME_API AFlecsEntitySpawner : public AActor
{
	GENERATED_BODY()

public:
	AFlecsEntitySpawner();

	// ═══════════════════════════════════════════════════════════════
	// ENTITY DEFINITION (main config - just set this!)
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Entity definition containing all profiles (physics, render, health, etc.)
	 * This is the ONLY required setting - everything else is derived from it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs Entity")
	TObjectPtr<UFlecsEntityDefinition> EntityDefinition;

	// ═══════════════════════════════════════════════════════════════
	// OVERRIDES (optional - override EntityDefinition values)
	// ═══════════════════════════════════════════════════════════════

	/** Override initial velocity from definition */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs Entity|Overrides")
	FVector InitialVelocity = FVector::ZeroVector;

	/** Override scale from RenderProfile */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs Entity|Overrides")
	bool bOverrideScale = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs Entity|Overrides",
		meta = (EditCondition = "bOverrideScale"))
	FVector ScaleOverride = FVector::OneVector;

	// ═══════════════════════════════════════════════════════════════
	// SPAWNER SETTINGS
	// ═══════════════════════════════════════════════════════════════

	/** Destroy this actor after spawning entity? (recommended for static placement) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs Entity|Spawner")
	bool bDestroyAfterSpawn = true;

	/** Spawn on BeginPlay? If false, call SpawnEntity() manually */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs Entity|Spawner")
	bool bSpawnOnBeginPlay = true;

	// ═══════════════════════════════════════════════════════════════
	// PREVIEW (editor only)
	// ═══════════════════════════════════════════════════════════════

	/** Show mesh preview in editor? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	bool bShowPreview = true;

	// ═══════════════════════════════════════════════════════════════
	// API
	// ═══════════════════════════════════════════════════════════════

	/** Get the spawned entity's key */
	UFUNCTION(BlueprintPure, Category = "Flecs Entity")
	FSkeletonKey GetEntityKey() const { return SpawnedEntityKey; }

	/** Manually spawn entity (use if bSpawnOnBeginPlay = false) */
	UFUNCTION(BlueprintCallable, Category = "Flecs Entity")
	FSkeletonKey SpawnEntity();

	/** Check if entity has been spawned */
	UFUNCTION(BlueprintPure, Category = "Flecs Entity")
	bool HasSpawned() const { return SpawnedEntityKey.IsValid(); }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UStaticMeshComponent> PreviewMeshComponent;

	FSkeletonKey SpawnedEntityKey;

	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void UpdatePreview();
#endif
};
