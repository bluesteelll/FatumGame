// Artillery-integrated Character implementation

#include "ArtilleryCharacter.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "FMasks.h"  // For Arty::Intents
#include "Engine/Engine.h"  // For GEngine debug messages

AArtilleryCharacter::AArtilleryCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Create Artillery components
	KeyCarry = CreateDefaultSubobject<UPlayerKeyCarry>(TEXT("KeyCarry"));
	BarrageMovement = CreateDefaultSubobject<UBarrageCharacterMovement>(TEXT("BarrageMovement"));

	// BarrageMovement handles everything automatically:
	// - WASD movement (reads MoveForward/MoveRight axes or falls back to raw keys)
	// - Jump (SpaceBar)
	// - Sprint (LeftShift)
	// - Position sync (Jolt → UE Actor)
	// Just set up Input Settings and it works!

	// Enable ticking
	PrimaryActorTick.bCanEverTick = true;
}

void AArtilleryCharacter::BeginPlay()
{
	Super::BeginPlay();
	bIsReady = true;
}

void AArtilleryCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// Movement handled automatically by BarrageCharacterMovement component
}

void AArtilleryCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Camera control only - movement handled by BarrageCharacterMovement
	if (bUseStandardCameraControl)
	{
		PlayerInputComponent->BindAxis("Turn", this, &AArtilleryCharacter::AddCameraYawInput);
		PlayerInputComponent->BindAxis("LookUp", this, &AArtilleryCharacter::AddCameraPitchInput);
	}
}

void AArtilleryCharacter::AddCameraPitchInput(float Value)
{
	if (Controller && !FMath::IsNearlyZero(Value))
	{
		APlayerController* PC = Cast<APlayerController>(Controller);
		if (PC)
		{
			PC->AddPitchInput(Value);
		}
	}
}

void AArtilleryCharacter::AddCameraYawInput(float Value)
{
	if (Controller && !FMath::IsNearlyZero(Value))
	{
		APlayerController* PC = Cast<APlayerController>(Controller);
		if (PC)
		{
			PC->AddYawInput(Value);
		}
	}
}

// ═══════════════════════════════════════════════════════════════
// IArtilleryLocomotionInterface Implementation
// ═══════════════════════════════════════════════════════════════

FSkeletonKey AArtilleryCharacter::GetMyKey() const
{
	return KeyCarry ? KeyCarry->GetMyKey() : FSkeletonKey();
}

bool AArtilleryCharacter::LocomotionStateMachine(FArtilleryShell PreviousMovement, FArtilleryShell Movement, bool RunAtLeastOnce, bool Smear)
{
	// Movement is handled by BarrageCharacterMovement component
	// This is called from Artillery thread - be careful with thread safety!

	if (!BarrageMovement || !BarrageMovement->IsReady)
	{
		return false;
	}

	// Get movement input from Artillery shell
	float ForwardInput = Movement.GetStickLeftY();
	float RightInput = Movement.GetStickLeftX();

	// Apply movement through Barrage physics
	if (!FMath::IsNearlyZero(ForwardInput) || !FMath::IsNearlyZero(RightInput))
	{
		FVector MoveDirection = GetActorForwardVector() * ForwardInput + GetActorRightVector() * RightInput;
		MoveDirection.Normalize();

		// Add force through Barrage
		BarrageMovement->AddOneTickOfForce(FVector3f(MoveDirection * BarrageMovement->MovementSpeed));
	}

	// Handle jump (A button = index 2)
	if (Movement.GetInputAction(Arty::Intents::AIndex))
	{
		BarrageMovement->Jump();
	}

	return true;
}

void AArtilleryCharacter::LookStateMachine(FRotator& IN_OUT_LookAxisVector)
{
	// Look handling is done by ABarragePlayerController
	// This can be used for additional look processing if needed
}

bool AArtilleryCharacter::IsReady()
{
	return bIsReady && BarrageMovement && BarrageMovement->IsReady;
}

void AArtilleryCharacter::PrepareForPossess()
{
	UE_LOG(LogTemp, Log, TEXT("AArtilleryCharacter::PrepareForPossess"));
}

void AArtilleryCharacter::PrepareForUnPossess()
{
	UE_LOG(LogTemp, Log, TEXT("AArtilleryCharacter::PrepareForUnPossess"));
}

// ═══════════════════════════════════════════════════════════════
// Projectile Firing
// ═══════════════════════════════════════════════════════════════

FVector AArtilleryCharacter::GetMuzzleLocation() const
{
	FVector Location = GetActorLocation();
	FVector Forward = GetActorForwardVector();
	FVector Right = GetActorRightVector();
	FVector Up = GetActorUpVector();

	// Apply muzzle offset in local space
	Location += Forward * MuzzleOffset.X;
	Location += Right * MuzzleOffset.Y;
	Location += Up * MuzzleOffset.Z;

	return Location;
}

FVector AArtilleryCharacter::GetFiringDirection() const
{
	// Try to get direction from controller (camera direction)
	if (Controller)
	{
		return Controller->GetControlRotation().Vector();
	}

	// Fallback to actor forward
	return GetActorForwardVector();
}

FSkeletonKey AArtilleryCharacter::FireProjectile()
{
	return FireProjectileInDirection(GetFiringDirection());
}

FSkeletonKey AArtilleryCharacter::FireProjectileInDirection(FVector Direction)
{
	if (!ProjectileDefinition)
	{
		UE_LOG(LogTemp, Error, TEXT("AArtilleryCharacter::FireProjectile - No ProjectileDefinition set! Assign it in Blueprint."));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("No ProjectileDefinition set!"));
		}
		return FSkeletonKey();
	}

	if (!ProjectileDefinition->ProjectileMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("AArtilleryCharacter::FireProjectile - ProjectileDefinition has no mesh!"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("ProjectileDefinition has no mesh!"));
		}
		return FSkeletonKey();
	}

	FVector SpawnLocation = GetMuzzleLocation();
	Direction.Normalize();

	UE_LOG(LogTemp, Warning, TEXT("Firing projectile: Location=%s, Direction=%s, Speed=%f"),
		*SpawnLocation.ToString(), *Direction.ToString(),
		ProjectileSpeedOverride > 0 ? ProjectileSpeedOverride : ProjectileDefinition->DefaultSpeed);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow,
			FString::Printf(TEXT("Firing at %s"), *SpawnLocation.ToString()));
	}

	FSkeletonKey ProjectileKey = UArtilleryLibrary::SpawnProjectileFromDefinition(
		GetWorld(),
		ProjectileDefinition,
		SpawnLocation,
		Direction,
		ProjectileSpeedOverride
	);

	if (ProjectileKey.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Projectile spawned with key: %llu"), static_cast<uint64>(ProjectileKey));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green, TEXT("Projectile spawned!"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to spawn projectile - SpawnProjectileFromDefinition returned invalid key"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("Spawn FAILED!"));
		}
	}

	return ProjectileKey;
}

TArray<FSkeletonKey> AArtilleryCharacter::FireProjectileSpread(int32 Count, float SpreadAngle)
{
	if (!ProjectileDefinition)
	{
		UE_LOG(LogTemp, Warning, TEXT("AArtilleryCharacter::FireProjectileSpread - No ProjectileDefinition set!"));
		return TArray<FSkeletonKey>();
	}

	FVector SpawnLocation = GetMuzzleLocation();
	FVector Direction = GetFiringDirection();

	return UArtilleryLibrary::SpawnProjectileSpreadFromDefinition(
		GetWorld(),
		ProjectileDefinition,
		SpawnLocation,
		Direction,
		Count,
		SpreadAngle,
		ProjectileSpeedOverride
	);
}
