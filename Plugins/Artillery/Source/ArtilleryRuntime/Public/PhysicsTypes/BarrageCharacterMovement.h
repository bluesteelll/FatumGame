// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Auto Movement Component for Barrage Character Physics
// Works like CharacterMovementComponent but for Barrage!

#pragma once

#include "CoreMinimal.h"
#include "BarragePlayerAgent.h"
#include "GameFramework/Pawn.h"
#include "BarrageCharacterMovement.generated.h"

/**
 * AUTOMATIC Character Movement for Barrage Physics
 *
 * Drop this component on your Character Blueprint and it will:
 * - Auto-handle WASD movement
 * - Auto-handle jumping
 * - Sync visual position with Barrage physics
 * - Work EXACTLY like CharacterMovementComponent
 *
 * NO Blueprint code needed!
 *
 * HOW TO USE:
 * 1. Add to Character Blueprint
 * 2. Set Input Bindings (MoveForward, MoveRight, Jump)
 * 3. Play!
 */
UCLASS(Blueprintable, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class ARTILLERYRUNTIME_API UBarrageCharacterMovement : public UBarragePlayerAgent
{
	GENERATED_BODY()

public:
	UBarrageCharacterMovement(const FObjectInitializer& ObjectInitializer);

	// ═══════════════════════════════════════════════════════════════
	// AUTO INPUT PROCESSING
	// ═══════════════════════════════════════════════════════════════

	/** Auto-process input (WASD, Jump)? Set to false to control manually from Blueprint */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Movement")
	bool bAutoProcessInput = true;

	/** Auto-sync Actor location with Barrage physics position? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Movement")
	bool bAutoSyncPosition = true;

	// ═══════════════════════════════════════════════════════════════
	// MOVEMENT SETTINGS
	// ═══════════════════════════════════════════════════════════════

	/** Movement speed multiplier (higher = faster) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Movement|Speed")
	float MovementSpeed = 1000.0f;

	/** Air control multiplier (0.0 = no air control, 1.0 = full control) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Movement|Speed")
	float AirControlMultiplier = 0.3f;

	/** Enable sprint? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Movement|Sprint")
	bool bEnableSprint = true;

	/** Sprint speed multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Movement|Sprint")
	float SprintSpeedMultiplier = 2.0f;

	// ═══════════════════════════════════════════════════════════════
	// INPUT BINDING NAMES
	// ═══════════════════════════════════════════════════════════════

	/** Input Axis name for forward/backward (default: "MoveForward") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Movement|Input Bindings")
	FName MoveForwardAxisName = "MoveForward";

	/** Input Axis name for left/right (default: "MoveRight") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Movement|Input Bindings")
	FName MoveRightAxisName = "MoveRight";

	/** Input Action name for jump (default: "Jump") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Movement|Input Bindings")
	FName JumpActionName = "Jump";

	/** Input Action name for sprint (default: "Sprint") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Barrage Movement|Input Bindings")
	FName SprintActionName = "Sprint";

	// ═══════════════════════════════════════════════════════════════
	// RUNTIME STATE
	// ═══════════════════════════════════════════════════════════════

	/** Is player currently sprinting? */
	UPROPERTY(BlueprintReadOnly, Category = "Barrage Movement")
	bool bIsSprinting = false;

	/** Is player on ground? */
	UPROPERTY(BlueprintReadOnly, Category = "Barrage Movement")
	bool bIsOnGround = false;

	// ═══════════════════════════════════════════════════════════════
	// MANUAL CONTROL (for Blueprint scripting)
	// ═══════════════════════════════════════════════════════════════

	/** Manually add movement input (like AddMovementInput in Character) */
	UFUNCTION(BlueprintCallable, Category = "Barrage Movement")
	void AddMovementInput(FVector WorldDirection, float ScaleValue = 1.0f);

	/** Try to jump */
	UFUNCTION(BlueprintCallable, Category = "Barrage Movement")
	void Jump();

	/** Stop jumping */
	UFUNCTION(BlueprintCallable, Category = "Barrage Movement")
	void StopJumping();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual bool RegistrationImplementation() override;

private:
	// Auto input processing
	void ProcessAutoInput();
	void ProcessMovementInput();
	void ProcessJumpInput();
	void SyncPositionWithPhysics();

	// Input state
	bool bWantsToJump = false;
	bool bWasJumpPressed = false;

	// Cached pawn reference
	APawn* CachedPawnOwner = nullptr;
};
