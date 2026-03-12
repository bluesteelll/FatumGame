// Light source actor combining a visual UE light with a Flecs stealth light entity.
// Uses unified UFlecsEntityLibrary::SpawnEntity API.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkeletonTypes.h"
#include "FlecsLightSourceActor.generated.h"

class UFlecsEntityDefinition;
class UPointLightComponent;

/**
 * Place in editor to create both a visual UE light and a Flecs stealth light entity.
 *
 * HOW TO USE:
 * 1. Drag to level
 * 2. Set LightDefinition (EntityDefinition with StealthLightProfile)
 * 3. Tune the PointLight component visuals in the editor
 * 4. Play!
 *
 * The LightDefinition drives the Flecs entity (gameplay stealth detection).
 * The PointLight component drives UE rendering (visual only).
 * They are independent — tune each separately.
 */
UCLASS(Blueprintable, BlueprintType)
class FATUMGAME_API AFlecsLightSourceActor : public AActor
{
	GENERATED_BODY()

public:
	AFlecsLightSourceActor();

	// ═══════════════════════════════════════════════════════════════
	// ENTITY DEFINITION (required)
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Entity definition with StealthLightProfile set.
	 * Drives the Flecs stealth light entity (gameplay detection).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stealth Light")
	TObjectPtr<UFlecsEntityDefinition> LightDefinition;

	// ═══════════════════════════════════════════════════════════════
	// VISUAL LIGHT (cosmetic only)
	// ═══════════════════════════════════════════════════════════════

	/** UE point light for visual rendering (independent of stealth light) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stealth Light")
	TObjectPtr<UPointLightComponent> PointLight;

	// ═══════════════════════════════════════════════════════════════
	// API
	// ═══════════════════════════════════════════════════════════════

	/** Get the spawned stealth light entity's key */
	UFUNCTION(BlueprintPure, Category = "Stealth Light")
	FSkeletonKey GetEntityKey() const { return SpawnedEntityKey; }

	/** Check if the stealth light entity has been spawned */
	UFUNCTION(BlueprintPure, Category = "Stealth Light")
	bool HasSpawned() const { return SpawnedEntityKey.IsValid(); }

protected:
	FSkeletonKey SpawnedEntityKey;

	virtual void BeginPlay() override;
};
