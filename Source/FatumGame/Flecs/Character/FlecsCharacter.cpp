
#include "FlecsCharacter.h"
#include "FlecsGameplayLibrary.h"
#include "FlecsProjectileDefinition.h"
#include "FlecsEntitySpawner.h"
#include "FlecsEntityDefinition.h"
#include "FlecsItemDefinition.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsStaticComponents.h"
#include "FlecsInstanceComponents.h"
#include "Engine/Engine.h"  // For GEngine->AddOnScreenDebugMessage
#include "EssentialTypes/PlayerKeyCarry.h"
#include "PhysicsTypes/BarrageCharacterMovement.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"

AFlecsCharacter::AFlecsCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Create Artillery/Flecs components
	KeyCarry = CreateDefaultSubobject<UPlayerKeyCarry>(TEXT("KeyCarry"));
	BarrageMovement = CreateDefaultSubobject<UBarrageCharacterMovement>(TEXT("BarrageMovement"));

	// Create camera boom (used in third-person mode)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	// Default: first-person - attach to root, will be reconfigured in BeginPlay
	FollowCamera->SetupAttachment(RootComponent);
	FollowCamera->bUsePawnControlRotation = true;

	PrimaryActorTick.bCanEverTick = true;
}

void AFlecsCharacter::BeginPlay()
{
	Super::BeginPlay();

	// ─────────────────────────────────────────────────────────────
	// CAMERA SETUP: First-person or Third-person
	// ─────────────────────────────────────────────────────────────
	if (bFirstPersonCamera)
	{
		// First-person: camera at eye level, no boom
		CameraBoom->SetActive(false);
		CameraBoom->SetVisibility(false);
		FollowCamera->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		FollowCamera->SetRelativeLocation(FVector(0.f, 0.f, 60.f)); // Eye height
		FollowCamera->bUsePawnControlRotation = true;
		bUseControllerRotationYaw = true;
		bUseControllerRotationPitch = false;
		bUseControllerRotationRoll = false;
	}
	else
	{
		// Third-person: camera on boom
		CameraBoom->SetActive(true);
		FollowCamera->AttachToComponent(CameraBoom, FAttachmentTransformRules::KeepRelativeTransform, USpringArmComponent::SocketName);
		FollowCamera->bUsePawnControlRotation = false;
		bUseControllerRotationYaw = false;
		bUseControllerRotationPitch = false;
		bUseControllerRotationRoll = false;
	}

	// ─────────────────────────────────────────────────────────────
	// ENHANCED INPUT: Add Input Mapping Context
	// ─────────────────────────────────────────────────────────────
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}

	// ─────────────────────────────────────────────────────────────
	// FLECS: Register character entity
	// Uses NEW Static/Instance architecture
	// ─────────────────────────────────────────────────────────────
	float InitialHealth = StartingHealth > 0.f ? StartingHealth : MaxHealth;
	CachedHealth = InitialHealth;

	FSkeletonKey Key = GetEntityKey();
	if (Key.IsValid())
	{
		UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
		if (FlecsSubsystem)
		{
			float CapturedMaxHealth = MaxHealth;
			float CapturedInitialHealth = InitialHealth;
			float CapturedArmor = Armor;

			FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Key, CapturedMaxHealth, CapturedInitialHealth, CapturedArmor]()
			{
				flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
				if (!FlecsWorld) return;

				// Static data (could be shared via prefab in future)
				FHealthStatic HealthStatic;
				HealthStatic.MaxHP = CapturedMaxHealth;
				HealthStatic.Armor = CapturedArmor;
				HealthStatic.RegenPerSecond = 0.f;
				HealthStatic.bDestroyOnDeath = false;  // Characters handle death differently

				// Instance data (per-entity mutable state)
				FHealthInstance HealthInstance;
				HealthInstance.CurrentHP = CapturedInitialHealth;
				HealthInstance.RegenAccumulator = 0.f;

				flecs::entity Entity = FlecsWorld->entity()
					.set<FHealthStatic>(HealthStatic)
					.set<FHealthInstance>(HealthInstance)
					.add<FTagCharacter>();

				// Bidirectional binding: sets FBarrageBody + atomic in FBarragePrimitive
				FlecsSubsystem->BindEntityToBarrage(Entity, Key);

				UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: Registered in Flecs with Key=%llu, HP=%.0f/%.0f"),
					static_cast<uint64>(Key), CapturedInitialHealth, CapturedMaxHealth);
			});
		}
	}
}

void AFlecsCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unregister from Flecs
	FSkeletonKey Key = GetEntityKey();
	if (Key.IsValid())
	{
		UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
		if (FlecsSubsystem)
		{
			FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Key]()
			{
				// Lock-free O(1) lookup via bidirectional binding
				flecs::entity Entity = FlecsSubsystem->GetEntityForBarrageKey(Key);
				if (Entity.is_valid() && Entity.is_alive())
				{
					// Unbind clears both forward (FBarrageBody) and reverse (atomic) bindings
					FlecsSubsystem->UnbindEntityFromBarrage(Entity);
					Entity.destruct();
				}
			});
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AFlecsCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	CheckHealthChanges();
}

// ═══════════════════════════════════════════════════════════════════════════
// ENHANCED INPUT SETUP
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Cast to Enhanced Input Component
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Movement (WASD)
		if (MoveAction)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AFlecsCharacter::Move);
		}

		// Looking (Mouse)
		if (LookAction)
		{
			EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFlecsCharacter::Look);
		}

		// Jumping (Space)
		if (JumpAction)
		{
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		}

		// Firing (LMB)
		if (FireAction)
		{
			EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &AFlecsCharacter::StartFire);
		}

		// Spawn Item (E)
		if (SpawnItemAction)
		{
			EnhancedInputComponent->BindAction(SpawnItemAction, ETriggerEvent::Started, this, &AFlecsCharacter::OnSpawnItem);
		}

		// Destroy Item (F)
		if (DestroyItemAction)
		{
			EnhancedInputComponent->BindAction(DestroyItemAction, ETriggerEvent::Started, this, &AFlecsCharacter::OnDestroyItem);
		}
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// ENHANCED INPUT HANDLERS
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::Move(const FInputActionValue& Value)
{
	// Input is a Vector2D (X = Right/Left, Y = Forward/Back)
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// Get controller rotation and extract yaw only
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// Calculate forward and right directions
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// Add movement input
		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);
	}
}

void AFlecsCharacter::Look(const FInputActionValue& Value)
{
	// Input is a Vector2D (X = Yaw, Y = Pitch)
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// Add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

void AFlecsCharacter::StartFire(const FInputActionValue& Value)
{
	FireProjectile();
}

// ═══════════════════════════════════════════════════════════════════════════
// HEALTH
// ═══════════════════════════════════════════════════════════════════════════

float AFlecsCharacter::GetCurrentHealth() const
{
	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!FlecsSubsystem) return CachedHealth;

	float CurrentHP, MaxHP;
	if (UFlecsGameplayLibrary::GetHealth_ArtilleryThread(FlecsSubsystem, GetEntityKey(), CurrentHP, MaxHP))
	{
		return CurrentHP;
	}
	return CachedHealth;
}

float AFlecsCharacter::GetHealthPercent() const
{
	return MaxHealth > 0.f ? GetCurrentHealth() / MaxHealth : 0.f;
}

bool AFlecsCharacter::IsAlive() const
{
	return GetCurrentHealth() > 0.f;
}

void AFlecsCharacter::ApplyDamage(float Damage)
{
	if (Damage <= 0.f) return;
	UFlecsGameplayLibrary::ApplyDamageByBarrageKey(this, GetEntityKey(), Damage);
}

void AFlecsCharacter::Heal(float Amount)
{
	if (Amount <= 0.f) return;
	UFlecsGameplayLibrary::HealEntityByBarrageKey(this, GetEntityKey(), Amount);
}

void AFlecsCharacter::CheckHealthChanges()
{
	float CurrentHealth = GetCurrentHealth();

	if (!FMath::IsNearlyEqual(CurrentHealth, CachedHealth, 0.01f))
	{
		float Delta = CurrentHealth - CachedHealth;

		if (Delta < 0.f)
		{
			// Took damage
			OnDamageTaken(-Delta, CurrentHealth);
		}
		else
		{
			// Healed
			OnHealed(Delta, CurrentHealth);
		}

		// Check for death
		if (CurrentHealth <= 0.f && CachedHealth > 0.f)
		{
			HandleDeath();
		}

		CachedHealth = CurrentHealth;
	}
}

void AFlecsCharacter::HandleDeath()
{
	UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter: %s died!"), *GetName());
	OnDeath();
}

// ═══════════════════════════════════════════════════════════════════════════
// PROJECTILE
// ═══════════════════════════════════════════════════════════════════════════

FVector AFlecsCharacter::GetMuzzleLocation() const
{
	FVector Location = GetActorLocation();
	Location += GetActorForwardVector() * MuzzleOffset.X;
	Location += GetActorRightVector() * MuzzleOffset.Y;
	Location += GetActorUpVector() * MuzzleOffset.Z;
	return Location;
}

FVector AFlecsCharacter::GetFiringDirection() const
{
	if (Controller)
	{
		return Controller->GetControlRotation().Vector();
	}
	return GetActorForwardVector();
}

FSkeletonKey AFlecsCharacter::FireProjectile()
{
	return FireProjectileInDirection(GetFiringDirection());
}

FSkeletonKey AFlecsCharacter::FireProjectileInDirection(FVector Direction)
{
	if (!ProjectileDefinition || !ProjectileDefinition->Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter::FireProjectile - No ProjectileDefinition or mesh!"));
		return FSkeletonKey();
	}

	Direction.Normalize();

	return UFlecsGameplayLibrary::SpawnProjectileFromDefinition(
		GetWorld(),
		ProjectileDefinition,
		GetMuzzleLocation(),
		Direction,
		ProjectileSpeedOverride
	);
}

TArray<FSkeletonKey> AFlecsCharacter::FireProjectileSpread(int32 Count, float SpreadAngle)
{
	TArray<FSkeletonKey> Keys;

	if (!ProjectileDefinition || !ProjectileDefinition->Mesh)
	{
		return Keys;
	}

	FVector Direction = GetFiringDirection();
	Direction.Normalize();
	FRotator BaseRotation = Direction.Rotation();
	FVector SpawnLocation = GetMuzzleLocation();

	for (int32 i = 0; i < Count; ++i)
	{
		float AngleOffset = (i - (Count - 1) * 0.5f) * (SpreadAngle / FMath::Max(1, Count - 1));
		FRotator SpreadRotation = BaseRotation;
		SpreadRotation.Yaw += AngleOffset;
		SpreadRotation.Pitch += FMath::RandRange(-SpreadAngle * 0.25f, SpreadAngle * 0.25f);

		FSkeletonKey Key = UFlecsGameplayLibrary::SpawnProjectileFromDefinition(
			GetWorld(),
			ProjectileDefinition,
			SpawnLocation,
			SpreadRotation.Vector(),
			ProjectileSpeedOverride
		);

		if (Key.IsValid())
		{
			Keys.Add(Key);
		}
	}

	return Keys;
}

// ═══════════════════════════════════════════════════════════════════════════
// ENTITY SPAWNING
// ═══════════════════════════════════════════════════════════════════════════

FSkeletonKey AFlecsCharacter::SpawnTestEntity()
{
	if (!TestEntityDefinition)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter::SpawnTestEntity - No TestEntityDefinition set!"));
		return FSkeletonKey();
	}

	// Calculate spawn location in front of character
	FVector SpawnLocation = GetActorLocation() + GetActorForwardVector() * SpawnDistance;
	SpawnLocation.Z += 50.f;  // Lift slightly above ground

	FSkeletonKey Key = UFlecsEntityLibrary::SpawnEntityFromDefinition(
		this,
		TestEntityDefinition,
		SpawnLocation,
		GetActorRotation()
	);

	if (Key.IsValid())
	{
		SpawnedEntityKeys.Add(Key);
		UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: Spawned entity Key=%llu, Total=%d"),
			static_cast<uint64>(Key), SpawnedEntityKeys.Num());
	}

	return Key;
}

void AFlecsCharacter::DestroyLastSpawnedEntity()
{
	if (SpawnedEntityKeys.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter::DestroyLastSpawnedEntity - No entities to destroy!"));
		return;
	}

	FSkeletonKey Key = SpawnedEntityKeys.Pop();
	UFlecsEntityLibrary::DestroyEntity(this, Key);

	UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: Destroyed entity Key=%llu, Remaining=%d"),
		static_cast<uint64>(Key), SpawnedEntityKeys.Num());
}

void AFlecsCharacter::OnSpawnItem(const FInputActionValue& Value)
{
	// If container testing is configured, use container mode
	if (TestContainerDefinition && TestItemDefinition && TestItemDefinition->ItemDefinition)
	{
		// Spawn container if not yet spawned
		if (!TestContainerKey.IsValid())
		{
			SpawnTestContainer();
		}
		else
		{
			// Add item to existing container
			AddItemToTestContainer();
		}
	}
	else
	{
		// Fallback to entity spawning
		SpawnTestEntity();
	}
}

void AFlecsCharacter::OnDestroyItem(const FInputActionValue& Value)
{
	// If container testing is configured, use container mode
	if (TestContainerDefinition && TestContainerKey.IsValid())
	{
		RemoveAllItemsFromTestContainer();
	}
	else
	{
		// Fallback to entity destruction
		DestroyLastSpawnedEntity();
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// CONTAINER TESTING
// ═══════════════════════════════════════════════════════════════════════════

FSkeletonKey AFlecsCharacter::SpawnTestContainer()
{
	if (!TestContainerDefinition)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter::SpawnTestContainer - No TestContainerDefinition set!"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("No TestContainerDefinition set!"));
		}
		return FSkeletonKey();
	}

	// Spawn container in front of character
	FVector SpawnLocation = GetActorLocation() + GetActorForwardVector() * SpawnDistance;
	SpawnLocation.Z += 50.f;

	TestContainerKey = UFlecsEntityLibrary::SpawnEntityFromDefinition(
		this,
		TestContainerDefinition,
		SpawnLocation,
		GetActorRotation()
	);

	if (TestContainerKey.IsValid())
	{
		FString Message = FString::Printf(TEXT("Container spawned: %llu"), static_cast<uint64>(TestContainerKey));
		UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: %s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, Message);
		}
	}
	else
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("Failed to spawn container!"));
		}
	}

	return TestContainerKey;
}

bool AFlecsCharacter::AddItemToTestContainer()
{
	if (!TestContainerKey.IsValid())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("No container to add item to!"));
		}
		return false;
	}

	if (!TestItemDefinition)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("No TestItemDefinition set!"));
		}
		return false;
	}

	if (!TestItemDefinition->ItemDefinition)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("TestItemDefinition has no ItemDefinition profile!"));
		}
		return false;
	}

	int32 ActuallyAdded = 0;
	// Use new prefab-based function - stores EntityDefinition reference
	bool bSuccess = UFlecsEntityLibrary::AddItemToContainerFromDefinition(
		this,
		TestContainerKey,
		TestItemDefinition,  // Pass full EntityDefinition, not just ItemDefinition
		1,  // Add 1 item
		ActuallyAdded
	);

	// Get current count for display
	int32 ItemCount = UFlecsEntityLibrary::GetContainerItemCount(this, TestContainerKey);

	FString ItemName = TestItemDefinition->ItemDefinition->ItemName.ToString();
	FString Message = FString::Printf(TEXT("Added item: %s (Container now has %d items) [Prefab]"), *ItemName, ItemCount + 1);
	UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: %s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan, Message);
	}

	return bSuccess;
}

void AFlecsCharacter::RemoveAllItemsFromTestContainer()
{
	if (!TestContainerKey.IsValid())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, TEXT("No container!"));
		}
		return;
	}

	// Get count before removal for display
	int32 ItemCount = UFlecsEntityLibrary::GetContainerItemCount(this, TestContainerKey);

	// Remove all items
	UFlecsEntityLibrary::RemoveAllItemsFromContainer(this, TestContainerKey);

	FString Message = FString::Printf(TEXT("Removed all items from container (%d items removed)"), ItemCount);
	UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: %s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow, Message);
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// IDENTITY
// ═══════════════════════════════════════════════════════════════════════════

FSkeletonKey AFlecsCharacter::GetEntityKey() const
{
	return KeyCarry ? KeyCarry->GetMyKey() : FSkeletonKey();
}
