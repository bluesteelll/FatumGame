// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Flecs-integrated Character - full ECS integration for health, damage, collision.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SkeletonTypes.h"
#include "FlecsCharacter.generated.h"

class UFlecsProjectileDefinition;
class UBarrageCharacterMovement;
class UPlayerKeyCarry;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UCameraComponent;
struct FInputActionValue;

/**
 * Character fully integrated with Flecs ECS.
 *
 * Features:
 * - Barrage physics movement (Jolt engine)
 * - Flecs entity with FHealthData, FTagCharacter
 * - Automatic damage handling from projectiles
 * - Death callbacks
 *
 * Usage:
 * 1. Create Blueprint child
 * 2. Set ProjectileDefinition
 * 3. Use ABarragePlayerController
 */
UCLASS(Blueprintable)
class FATUMGAME_API AFlecsCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AFlecsCharacter(const FObjectInitializer& ObjectInitializer);

	// ═══════════════════════════════════════════════════════════════
	// COMPONENTS
	// ═══════════════════════════════════════════════════════════════

	/** Key carrier for Artillery/Flecs identity */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flecs")
	TObjectPtr<UPlayerKeyCarry> KeyCarry;

	/** Barrage physics movement */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flecs")
	TObjectPtr<UBarrageCharacterMovement> BarrageMovement;

	/** Camera boom (only used in third-person mode) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	/** If true, camera is first-person (attached to mesh). If false, third-person with boom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bFirstPersonCamera = true;

	// ═══════════════════════════════════════════════════════════════
	// ENHANCED INPUT
	// ═══════════════════════════════════════════════════════════════

	/** Input Mapping Context - assign in Blueprint */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	/** Fire Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> FireAction;

	// ═══════════════════════════════════════════════════════════════
	// HEALTH
	// ═══════════════════════════════════════════════════════════════

	/** Maximum health */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Health")
	float MaxHealth = 100.f;

	/** Starting health (0 = use MaxHealth) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Health")
	float StartingHealth = 0.f;

	/** Armor reduces incoming damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Health")
	float Armor = 0.f;

	/** Get current health from Flecs */
	UFUNCTION(BlueprintPure, Category = "Flecs|Health")
	float GetCurrentHealth() const;

	/** Get health as 0-1 percentage */
	UFUNCTION(BlueprintPure, Category = "Flecs|Health")
	float GetHealthPercent() const;

	/** Is the character alive? */
	UFUNCTION(BlueprintPure, Category = "Flecs|Health")
	bool IsAlive() const;

	/** Apply damage (goes through Flecs) */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Health")
	void ApplyDamage(float Damage);

	/** Heal (goes through Flecs) */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Health")
	void Heal(float Amount);

	// ═══════════════════════════════════════════════════════════════
	// PROJECTILE
	// ═══════════════════════════════════════════════════════════════

	/** Projectile definition (create in Content Browser) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Projectile")
	TObjectPtr<UFlecsProjectileDefinition> ProjectileDefinition;

	/** Muzzle offset from character origin */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Projectile")
	FVector MuzzleOffset = FVector(100.f, 0.f, 50.f);

	/** Speed override (0 = use definition) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flecs|Projectile")
	float ProjectileSpeedOverride = 0.f;

	/** Fire single projectile forward */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Projectile")
	FSkeletonKey FireProjectile();

	/** Fire projectile in direction */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Projectile")
	FSkeletonKey FireProjectileInDirection(FVector Direction);

	/** Fire spread (shotgun) */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Projectile")
	TArray<FSkeletonKey> FireProjectileSpread(int32 Count = 8, float SpreadAngle = 15.f);

	// ═══════════════════════════════════════════════════════════════
	// IDENTITY
	// ═══════════════════════════════════════════════════════════════

	/** Get SkeletonKey for this character */
	UFUNCTION(BlueprintPure, Category = "Flecs")
	FSkeletonKey GetEntityKey() const;

	// ═══════════════════════════════════════════════════════════════
	// EVENTS
	// ═══════════════════════════════════════════════════════════════

	/** Called when character takes damage */
	UFUNCTION(BlueprintImplementableEvent, Category = "Flecs|Events")
	void OnDamageTaken(float Damage, float NewHealth);

	/** Called when character dies */
	UFUNCTION(BlueprintImplementableEvent, Category = "Flecs|Events")
	void OnDeath();

	/** Called when character is healed */
	UFUNCTION(BlueprintImplementableEvent, Category = "Flecs|Events")
	void OnHealed(float Amount, float NewHealth);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** Get muzzle world location */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Projectile")
	FVector GetMuzzleLocation() const;

	/** Get firing direction (camera or forward) */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Projectile")
	FVector GetFiringDirection() const;

	/** Native death handler - calls OnDeath BP event */
	virtual void HandleDeath();

	// ═══════════════════════════════════════════════════════════════
	// ENHANCED INPUT HANDLERS
	// ═══════════════════════════════════════════════════════════════

	/** Called for movement input (WASD) */
	void Move(const FInputActionValue& Value);

	/** Called for looking input (Mouse) */
	void Look(const FInputActionValue& Value);

	/** Called when Fire is pressed */
	void StartFire(const FInputActionValue& Value);

private:
	/** Cached health for change detection */
	float CachedHealth = 0.f;

	/** Check for health changes from Flecs and fire events */
	void CheckHealthChanges();
};
