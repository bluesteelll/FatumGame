// Artillery-integrated Character for single-player games
// Supports Barrage physics and Artillery projectile system

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "EssentialTypes/ArtilleryActorControllerConcepts.h"
#include "EssentialTypes/PlayerKeyCarry.h"
#include "PhysicsTypes/BarrageCharacterMovement.h"
#include "Systems/ArtilleryBPLibs.h"
#include "BasicTypes/ProjectileDefinition.h"
#include "ArtilleryCharacter.generated.h"

/**
 * Base Character class integrated with Artillery/Barrage systems.
 *
 * Features:
 * - Barrage physics for movement (Jolt engine)
 * - Artillery projectile spawning support
 * - Compatible with ABarragePlayerController
 *
 * Usage:
 * 1. Create Blueprint child of this class
 * 2. Set up your mesh/camera
 * 3. Use ABarragePlayerController as PlayerController
 * 4. Call FireProjectile() to shoot
 */
UCLASS(Blueprintable)
class AArtilleryCharacter : public ACharacter, public IArtilleryLocomotionInterface
{
	GENERATED_BODY()

public:
	AArtilleryCharacter(const FObjectInitializer& ObjectInitializer);

	// ═══════════════════════════════════════════════════════════════
	// COMPONENTS
	// ═══════════════════════════════════════════════════════════════

	/** Key carrier for Artillery identity system */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Artillery")
	UPlayerKeyCarry* KeyCarry;

	/** Barrage physics movement component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Artillery")
	UBarrageCharacterMovement* BarrageMovement;

	// ═══════════════════════════════════════════════════════════════
	// PROJECTILE SETTINGS
	// ═══════════════════════════════════════════════════════════════

	/** Projectile definition to fire (set in Blueprint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artillery|Projectile")
	UProjectileDefinition* ProjectileDefinition;

	/** Offset from actor location for projectile spawn */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artillery|Projectile")
	FVector MuzzleOffset = FVector(100.0f, 0.0f, 50.0f);

	/** Override projectile speed (0 = use definition default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artillery|Projectile")
	float ProjectileSpeedOverride = 0.0f;

	// ═══════════════════════════════════════════════════════════════
	// FIRING
	// ═══════════════════════════════════════════════════════════════

	/** Fire a single projectile in the direction the character is facing */
	UFUNCTION(BlueprintCallable, Category = "Artillery|Projectile")
	FSkeletonKey FireProjectile();

	/** Fire a projectile in a specific direction */
	UFUNCTION(BlueprintCallable, Category = "Artillery|Projectile")
	FSkeletonKey FireProjectileInDirection(FVector Direction);

	/** Fire a spread of projectiles (shotgun effect) */
	UFUNCTION(BlueprintCallable, Category = "Artillery|Projectile")
	TArray<FSkeletonKey> FireProjectileSpread(int32 Count = 8, float SpreadAngle = 15.0f);

	// ═══════════════════════════════════════════════════════════════
	// IArtilleryLocomotionInterface Implementation
	// ═══════════════════════════════════════════════════════════════

	virtual FSkeletonKey GetMyKey() const override;
	virtual bool LocomotionStateMachine(FArtilleryShell PreviousMovement, FArtilleryShell Movement, bool RunAtLeastOnce, bool Smear) override;
	virtual void LookStateMachine(FRotator& IN_OUT_LookAxisVector) override;
	virtual bool IsReady() override;
	virtual void PrepareForPossess() override;
	virtual void PrepareForUnPossess() override;

	// ═══════════════════════════════════════════════════════════════
	// CAMERA CONTROL (works with standard PlayerController)
	// ═══════════════════════════════════════════════════════════════

	/** Use standard UE camera control instead of Artillery system */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Artillery|Camera")
	bool bUseStandardCameraControl = true;

	/** Add pitch input (look up/down) */
	UFUNCTION(BlueprintCallable, Category = "Artillery|Camera")
	void AddCameraPitchInput(float Value);

	/** Add yaw input (look left/right) */
	UFUNCTION(BlueprintCallable, Category = "Artillery|Camera")
	void AddCameraYawInput(float Value);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** Get the muzzle location for projectile spawning */
	UFUNCTION(BlueprintCallable, Category = "Artillery|Projectile")
	FVector GetMuzzleLocation() const;

	/** Get the firing direction (character forward or camera direction) */
	UFUNCTION(BlueprintCallable, Category = "Artillery|Projectile")
	FVector GetFiringDirection() const;

private:
	bool bIsReady = false;
};
