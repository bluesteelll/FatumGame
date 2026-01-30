// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "FlecsCharacter.h"
#include "FlecsGameplayLibrary.h"
#include "FlecsProjectileDefinition.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsComponents.h"
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

				flecs::entity Entity = FlecsWorld->entity()
					.set<FHealthData>({ CapturedInitialHealth, CapturedMaxHealth, CapturedArmor })
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
// IDENTITY
// ═══════════════════════════════════════════════════════════════════════════

FSkeletonKey AFlecsCharacter::GetEntityKey() const
{
	return KeyCarry ? KeyCarry->GetMyKey() : FSkeletonKey();
}
