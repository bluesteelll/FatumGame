// Artillery-integrated Character implementation

#include "ArtilleryCharacter.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "FMasks.h"  // For Arty::Intents
#include "Engine/Engine.h"  // For GEngine debug messages
#include "EnaceDispatch.h"  // For inventory

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

	// Create player inventory container
	if (UEnaceDispatch* Enace = UEnaceDispatch::Get(GetWorld()))
	{
		InventoryKey = Enace->CreateContainer(nullptr, InventorySlotCount, GetMyKey());
		UE_LOG(LogTemp, Log, TEXT("AArtilleryCharacter: Created inventory with %d slots (Key: %llu)"),
			InventorySlotCount, (uint64)InventoryKey);
	}
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

// ═══════════════════════════════════════════════════════════════
// Inventory (Enace Integration)
// ═══════════════════════════════════════════════════════════════

bool AArtilleryCharacter::AddItemToInventory(UEnaceItemDefinition* Definition, int32 Count)
{
	if (!Definition || Count <= 0)
	{
		return false;
	}

	UEnaceDispatch* Enace = UEnaceDispatch::Get(GetWorld());
	if (!Enace || !InventoryKey.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("AddItemToInventory: Enace not available or inventory not created"));
		return false;
	}

	FEnaceAddItemResult Result = Enace->AddItem(InventoryKey, Definition, Count);

	if (Result.bSuccess)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green,
				FString::Printf(TEXT("Added %s x%d to inventory (slot %d)"),
					*Definition->ItemId.ToString(),
					Result.AddedCount,
					Result.SlotIndex));
		}

		if (Result.OverflowCount > 0)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow,
					FString::Printf(TEXT("Overflow: %d items didn't fit"), Result.OverflowCount));
			}
		}
	}
	else
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red,
				TEXT("Inventory full!"));
		}
	}

	return Result.bSuccess;
}

bool AArtilleryCharacter::HasItemInInventory(UEnaceItemDefinition* Definition, int32 MinCount) const
{
	UEnaceDispatch* Enace = UEnaceDispatch::Get(GetWorld());
	if (!Enace || !InventoryKey.IsValid())
	{
		return false;
	}
	return Enace->HasItem(InventoryKey, Definition, MinCount);
}

int32 AArtilleryCharacter::GetItemCountInInventory(UEnaceItemDefinition* Definition) const
{
	UEnaceDispatch* Enace = UEnaceDispatch::Get(GetWorld());
	if (!Enace || !InventoryKey.IsValid())
	{
		return 0;
	}
	return Enace->GetItemCount(InventoryKey, Definition);
}

void AArtilleryCharacter::TestAddItem()
{
	if (!TestItemDefinition)
	{
		UE_LOG(LogTemp, Warning, TEXT("TestAddItem: TestItemDefinition is not set! Assign it in Blueprint."));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
				TEXT("TestItemDefinition not set! Assign in Blueprint."));
		}
		return;
	}

	AddItemToInventory(TestItemDefinition, 1);
}

void AArtilleryCharacter::TestAddItemToWorldChest()
{
	if (!TestItemDefinition)
	{
		UE_LOG(LogTemp, Warning, TEXT("TestAddItemToWorldChest: TestItemDefinition is not set!"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
				TEXT("TestItemDefinition not set! Assign in Blueprint."));
		}
		return;
	}

	UEnaceDispatch* Enace = UEnaceDispatch::Get(GetWorld());
	if (!Enace)
	{
		UE_LOG(LogTemp, Warning, TEXT("TestAddItemToWorldChest: EnaceDispatch not available"));
		return;
	}

	// Find first world container (chest on scene)
	FSkeletonKey ChestKey = Enace->FindFirstWorldContainer();
	if (!ChestKey.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("TestAddItemToWorldChest: No world container found! Place a chest (item with bIsContainer=true) on the level."));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
				TEXT("No chest found on level!"));
		}
		return;
	}

	// Add item to chest
	FEnaceAddItemResult Result = Enace->AddItem(ChestKey, TestItemDefinition, 1);

	if (Result.bSuccess)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green,
				FString::Printf(TEXT("Added %s to chest (slot %d)"),
					*TestItemDefinition->ItemId.ToString(),
					Result.SlotIndex));
		}
	}
	else
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red,
				TEXT("Chest is full!"));
		}
	}
}
