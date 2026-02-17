
#include "FlecsCharacter.h"
#include "FlecsDamageLibrary.h"
#include "FlecsWeaponLibrary.h"
#include "FlecsContainerLibrary.h"
#include "FlecsSpawnLibrary.h"
#include "FlecsEntitySpawner.h"
#include "FlecsEntityDefinition.h"
#include "FlecsRenderProfile.h"
#include "FlecsProjectileProfile.h"
#include "FlecsWeaponProfile.h"
#include "FlecsItemDefinition.h"
#include "FlecsContainerProfile.h"
#include "FlecsInteractionProfile.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsStaticComponents.h"
#include "FlecsInstanceComponents.h"
#include "FlecsWeaponComponents.h"
#include "Engine/Engine.h"  // For GEngine->AddOnScreenDebugMessage
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FBShapeParams.h"
#include "EPhysicsLayer.h"
#include "Skeletonize.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Components/SkeletalMeshComponent.h"
#include "Async/Async.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"
#include "FlecsHUDWidget.h"
#include "FlecsInventoryWidget.h"
#include "FlecsLootPanel.h"

AFlecsCharacter::AFlecsCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
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

	// Create weapon mesh component (attached to camera for FPS view)
	WeaponMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	WeaponMeshComponent->SetupAttachment(FollowCamera);
	WeaponMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMeshComponent->CastShadow = false;
	WeaponMeshComponent->SetVisibility(false);

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
			if (GameplayMappingContext)
			{
				Subsystem->AddMappingContext(GameplayMappingContext, 0);
			}
		}

		if (!CancelAction)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("AFlecsCharacter::BeginPlay: CancelAction is not set! "
					 "Escape key will not work for exiting interactions/menus. "
					 "Assign IA_Cancel and map it in BOTH GameplayMappingContext AND InventoryMappingContext."));
		}
	}

	// ─────────────────────────────────────────────────────────────
	// IDENTITY: Generate key from actor pointer hash
	// Replaces UPlayerKeyCarry component
	// ─────────────────────────────────────────────────────────────
	CharacterKey = MAKE_ACTORKEY(this);

	// ─────────────────────────────────────────────────────────────
	// FLECS: Register character entity
	// Uses NEW Static/Instance architecture
	// ─────────────────────────────────────────────────────────────
	float InitialHealth = StartingHealth > 0.f ? StartingHealth : MaxHealth;
	CachedHealth = InitialHealth;

	FSkeletonKey Key = CharacterKey;
	if (Key.IsValid())
	{
		UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
		if (FlecsSubsystem)
		{
			float CapturedMaxHealth = MaxHealth;
			float CapturedInitialHealth = InitialHealth;
			float CapturedArmor = Armor;
			FVector SpawnLocation = GetActorLocation();
			float CapsuleRadius = GetCapsuleComponent()->GetScaledCapsuleRadius();
			float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

			FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Key, CapturedMaxHealth, CapturedInitialHealth,
				CapturedArmor, SpawnLocation, CapsuleRadius, CapsuleHalfHeight]()
			{
				flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
				if (!FlecsWorld) return;

				// Create Barrage physics body for character (sensor for collision detection).
				// This body mirrors the UE actor position — real movement/gravity is handled
				// by UCharacterMovementComponent. GravityFactor=0 prevents Jolt from
				// pulling the sensor body down between SetPosition updates.
				UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
				if (Physics)
				{
					FBBoxParams BoxParams = FBarrageBounder::GenerateBoxBounds(
						SpawnLocation,
						CapsuleRadius * 2.0, CapsuleRadius * 2.0, CapsuleHalfHeight * 2.0,
						FVector3d::ZeroVector, FMassByCategory::MostEnemies);

					FBLet Body = Physics->CreatePrimitive(
						BoxParams, Key,
						static_cast<uint16>(EPhysicsLayer::MOVING),
						true,  // sensor - collision detection only
						false, // not force dynamic
						true); // movable

					if (FBarragePrimitive::IsNotNull(Body))
					{
						FBarragePrimitive::SetGravityFactor(0.0f, Body);
					}
				}

				// Static data
				FHealthStatic HealthStatic;
				HealthStatic.MaxHP = CapturedMaxHealth;
				HealthStatic.Armor = CapturedArmor;
				HealthStatic.RegenPerSecond = 0.f;
				HealthStatic.bDestroyOnDeath = false;

				// Instance data
				FHealthInstance HealthInstance;
				HealthInstance.CurrentHP = CapturedInitialHealth;
				HealthInstance.RegenAccumulator = 0.f;

				flecs::entity Entity = FlecsWorld->entity()
					.set<FHealthStatic>(HealthStatic)
					.set<FHealthInstance>(HealthInstance)
					.add<FTagCharacter>();

				// Bidirectional binding: sets FBarrageBody + atomic in FBarragePrimitive
				FlecsSubsystem->BindEntityToBarrage(Entity, Key);

				// Send initial health to HUD (sim thread → EnqueueMessage)
				if (UFlecsMessageSubsystem::SelfPtr)
				{
					FUIHealthMessage HealthMsg;
					HealthMsg.EntityId = static_cast<int64>(Entity.id());
					HealthMsg.CurrentHP = CapturedInitialHealth;
					HealthMsg.MaxHP = CapturedMaxHealth;
					UFlecsMessageSubsystem::SelfPtr->EnqueueMessage(TAG_UI_Health, HealthMsg);
				}

				UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: Registered in Flecs with Key=%llu, HP=%.0f/%.0f"),
					static_cast<uint64>(Key), CapturedInitialHealth, CapturedMaxHealth);
			});
		}

		UFlecsArtillerySubsystem::RegisterLocalPlayer(this, Key);

		// Spawn inventory containers (pure ECS, no physics)
		if (InventoryDefinition || WeaponInventoryDefinition)
		{
			UFlecsEntityDefinition* InvDef = InventoryDefinition;
			UFlecsEntityDefinition* WepInvDef = WeaponInventoryDefinition;
			FlecsSubsystem->EnqueueCommand([this, FlecsSubsystem, InvDef, WepInvDef, Key]()
			{
				flecs::world* World = FlecsSubsystem->GetFlecsWorld();
				if (!World) return;

				int64 CharEntityId = 0;
				flecs::entity CharEntity = FlecsSubsystem->GetEntityForBarrageKey(Key);
				if (CharEntity.is_valid())
				{
					CharEntityId = static_cast<int64>(CharEntity.id());
				}

				auto SpawnContainer = [&](UFlecsEntityDefinition* Def) -> int64
				{
					if (!Def || !Def->ContainerProfile) return 0;

					flecs::entity Prefab = FlecsSubsystem->GetOrCreateEntityPrefab(Def);
					if (!Prefab.is_valid()) return 0;

					flecs::entity E = World->entity().is_a(Prefab).add<FTagContainer>();

					FContainerInstance Inst;
					Inst.OwnerEntityId = CharEntityId;
					E.set<FContainerInstance>(Inst);

					const FContainerStatic* Static = E.try_get<FContainerStatic>();
					if (Static)
					{
						switch (Static->Type)
						{
						case EContainerType::Slot:
							{
								FContainerSlotsInstance SlotsInst;
								E.set<FContainerSlotsInstance>(SlotsInst);
							}
							break;
						case EContainerType::Grid:
							{
								FContainerGridInstance Grid;
								Grid.Initialize(Static->GridWidth, Static->GridHeight);
								E.set<FContainerGridInstance>(Grid);
							}
							break;
						case EContainerType::List:
							break;
						}
					}

					return static_cast<int64>(E.id());
				};

				int64 InvId = SpawnContainer(InvDef);
				int64 WepInvId = SpawnContainer(WepInvDef);

				AsyncTask(ENamedThreads::GameThread, [this, InvId, WepInvId]()
				{
					InventoryEntityId = InvId;
					WeaponInventoryEntityId = WepInvId;
					UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: Inventories spawned (General=%lld, Weapon=%lld)"),
						InvId, WepInvId);
				});
			});
		}
	}

	// Auto-equip weapon if TestWeaponDefinition is set
	if (TestWeaponDefinition && TestWeaponDefinition->WeaponProfile)
	{
		SpawnAndEquipTestWeapon();
	}

	// Start 10 Hz interaction trace timer
	GetWorld()->GetTimerManager().SetTimer(
		InteractionTraceTimerHandle,
		this,
		&AFlecsCharacter::PerformInteractionTrace,
		0.1f, // 10 Hz
		true   // looping
	);

	// Create HUD widget
	if (HUDWidgetClass)
	{
		if (APlayerController* PC = Cast<APlayerController>(Controller))
		{
			HUDWidget = CreateWidget<UFlecsHUDWidget>(PC, HUDWidgetClass);
			if (HUDWidget)
			{
				HUDWidget->AddToViewport();
			}
		}
	}

	// Create inventory widget (hidden by default)
	if (InventoryWidgetClass)
	{
		if (APlayerController* PC = Cast<APlayerController>(Controller))
		{
			InventoryWidget = CreateWidget<UFlecsInventoryWidget>(PC, InventoryWidgetClass);
			if (InventoryWidget)
			{
				InventoryWidget->SetOwningCharacter(this);
				InventoryWidget->GameplayMappingContext = GameplayMappingContext;
				InventoryWidget->PanelMappingContext = InventoryMappingContext;
				InventoryWidget->AddToViewport(10); // Above HUD
				InventoryWidget->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	// Create loot panel (hidden by default)
	if (LootPanelClass)
	{
		if (APlayerController* PC = Cast<APlayerController>(Controller))
		{
			LootPanel = CreateWidget<UFlecsLootPanel>(PC, LootPanelClass);
			if (LootPanel)
			{
				LootPanel->SetOwningCharacter(this);
				LootPanel->GameplayMappingContext = GameplayMappingContext;
				LootPanel->PanelMappingContext = InventoryMappingContext;
				LootPanel->AddToViewport(10);
				LootPanel->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
}

void AFlecsCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (LootPanel)
	{
		LootPanel->CloseLoot();
		LootPanel->RemoveFromParent();
		LootPanel = nullptr;
	}

	if (InventoryWidget)
	{
		InventoryWidget->CloseInventory();
		InventoryWidget->RemoveFromParent();
		InventoryWidget = nullptr;
	}

	if (HUDWidget)
	{
		HUDWidget->RemoveFromParent();
		HUDWidget = nullptr;
	}

	InventoryEntityId = 0;
	WeaponInventoryEntityId = 0;

	// Cleanup active interaction state
	if (InteractionState != EInteractionState::Gameplay)
	{
		CloseFocusPanel();
		RestoreCameraControl();
		InteractionState = EInteractionState::Gameplay;
		ActiveInteractionProfile = nullptr;
		ActiveInteractionTargetKey = FSkeletonKey();
	}

	GetWorld()->GetTimerManager().ClearTimer(InteractionTraceTimerHandle);
	CurrentInteractionTarget = FSkeletonKey();

	DetachWeaponVisual();
	UFlecsArtillerySubsystem::UnregisterLocalPlayer();

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
	TickInteractionStateMachine(DeltaTime);

	// Process pending weapon equip (set by sim thread via atomics).
	// Must run in Tick(), not AsyncTask(GameThread) — AsyncTask can fire during
	// post-tick component update phase, causing assert !bPostTickComponentUpdate.
	if (PendingWeaponEquip.bPending.load(std::memory_order_acquire))
	{
		PendingWeaponEquip.bPending.store(false, std::memory_order_relaxed);
		int64 WeaponId = PendingWeaponEquip.WeaponId.load(std::memory_order_acquire);

		TestWeaponEntityId = WeaponId;
		if (HUDWidget)
		{
			HUDWidget->SetWeaponEntityId(WeaponId);
		}
		AttachWeaponVisual(PendingWeaponEquip.Mesh, PendingWeaponEquip.AttachOffset);
		UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: Weapon spawned and equipped (EntityId=%lld)"), WeaponId);

		if (bPendingFireAfterSpawn)
		{
			bPendingFireAfterSpawn = false;
			StartFiringWeapon();
		}
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();

	// Sync UE actor position → Barrage physics body
	if (CharacterKey.IsValid() && FlecsSubsystem)
	{
		FVector Pos = GetActorLocation();
		FlecsSubsystem->EnqueueCommand([Key = CharacterKey, Pos]()
		{
			UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
			if (Physics)
			{
				FBLet Body = Physics->GetShapeRef(Key);
				if (FBarragePrimitive::IsNotNull(Body))
				{
					FBarragePrimitive::SetPosition(Pos, Body);
				}
			}
		});
	}

	// Continuously update aim direction, muzzle offset, and position so WeaponFireSystem has fresh data.
	// Uses lock-free TTripleBuffer — sim thread reads latest value right before ProgressWorld().
	// CharacterPosition = camera position (not actor center) to eliminate crosshair parallax.
	int64 CharId = GetCharacterEntityId();
	if (CharId != 0 && FlecsSubsystem)
	{
		if (auto* Bridge = FlecsSubsystem->GetLateSyncBridge())
		{
			// Use camera position as projectile spawn origin — matches crosshair exactly.
			// Actor position (capsule center) is ~60 units below camera, causing parallax.
			FVector SpawnOrigin = FollowCamera ? FollowCamera->GetComponentLocation() : GetActorLocation();
			FVector AimDir = GetFiringDirection();

			FAimDirection Aim;
			Aim.Direction = AimDir;
			Aim.CharacterPosition = SpawnOrigin;
			Aim.MuzzleWorldPosition = GetMuzzleLocation();
			Bridge->WriteAimDirection(CharId, Aim);
		}

		// One-time: pass player entity ID to HUD for message filtering
		if (HUDWidget && HUDWidget->CachedPlayerEntityId == 0)
		{
			HUDWidget->SetPlayerEntityId(CharId);
		}
	}
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
			EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &AFlecsCharacter::StopFire);
		}

		// Interact (E) — Started for press, Completed for release (Hold detection)
		if (SpawnItemAction)
		{
			EnhancedInputComponent->BindAction(SpawnItemAction, ETriggerEvent::Started, this, &AFlecsCharacter::OnSpawnItem);
			EnhancedInputComponent->BindAction(SpawnItemAction, ETriggerEvent::Completed, this, &AFlecsCharacter::OnInteractReleased);
		}

		// Destroy Item (F)
		if (DestroyItemAction)
		{
			EnhancedInputComponent->BindAction(DestroyItemAction, ETriggerEvent::Started, this, &AFlecsCharacter::OnDestroyItem);
		}

		// Toggle Inventory (I)
		if (InventoryAction)
		{
			EnhancedInputComponent->BindAction(InventoryAction, ETriggerEvent::Started, this, &AFlecsCharacter::ToggleInventory);
		}

		// Cancel / Exit (Escape)
		if (CancelAction)
		{
			EnhancedInputComponent->BindAction(CancelAction, ETriggerEvent::Started, this, &AFlecsCharacter::OnInteractCancel);
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
	// If we have a weapon, use the weapon system
	if (TestWeaponDefinition)
	{
		// Spawn weapon if not yet spawned
		if (TestWeaponEntityId == 0)
		{
			SpawnAndEquipTestWeapon();
			// Weapon spawns async — fire request would be lost.
			// Set flag so SpawnAndEquipTestWeapon fires after weapon is ready.
			bPendingFireAfterSpawn = true;
			return;
		}
		StartFiringWeapon();
	}
	else
	{
		// Fallback to direct projectile spawning
		FireProjectile();
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// HEALTH
// ═══════════════════════════════════════════════════════════════════════════

float AFlecsCharacter::GetCurrentHealth() const
{
	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!FlecsSubsystem) return CachedHealth;

	float CurrentHP, MaxHP;
	if (UFlecsDamageLibrary::GetHealth_ArtilleryThread(FlecsSubsystem, GetEntityKey(), CurrentHP, MaxHP))
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
	UFlecsDamageLibrary::ApplyDamageByBarrageKey(this, GetEntityKey(), Damage);
}

void AFlecsCharacter::Heal(float Amount)
{
	if (Amount <= 0.f) return;
	UFlecsDamageLibrary::HealEntityByBarrageKey(this, GetEntityKey(), Amount);
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

			// Force cancel active interaction on damage
			if (InteractionState != EInteractionState::Gameplay)
			{
				ForceCancelInteraction();
			}
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
	// Prefer weapon mesh socket if available (follows animation: recoil, sway, etc.)
	if (WeaponMeshComponent && WeaponMeshComponent->GetSkeletalMeshAsset())
	{
		static const FName MuzzleSocketName(TEXT("Muzzle"));
		if (WeaponMeshComponent->DoesSocketExist(MuzzleSocketName))
		{
			return WeaponMeshComponent->GetSocketLocation(MuzzleSocketName);
		}
	}

	// Fallback: camera position + aim-relative offset.
	// Must match sim thread computation (CharacterPosition + AimQuat * MuzzleOffset).
	// Using camera (not ActorLocation) eliminates parallax with crosshair.
	FVector CameraPos = FollowCamera ? FollowCamera->GetComponentLocation() : GetActorLocation();
	FVector AimDir = GetFiringDirection();
	FQuat AimQuat = FRotationMatrix::MakeFromX(AimDir).ToQuat();
	FTransform MuzzleTransform(AimQuat, CameraPos);
	return MuzzleTransform.TransformPosition(MuzzleOffset);
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
	if (!ProjectileDefinition || !ProjectileDefinition->RenderProfile || !ProjectileDefinition->RenderProfile->Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter::FireProjectile - No ProjectileDefinition or RenderProfile with mesh!"));
		return FSkeletonKey();
	}

	if (!ProjectileDefinition->ProjectileProfile)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter::FireProjectile - No ProjectileProfile!"));
		return FSkeletonKey();
	}

	Direction.Normalize();

	// Get owner entity ID for friendly fire prevention
	int64 OwnerEntityId = 0;
	if (UFlecsArtillerySubsystem* Subsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>())
	{
		flecs::entity OwnerEntity = Subsystem->GetEntityForBarrageKey(GetEntityKey());
		if (OwnerEntity.is_valid())
		{
			OwnerEntityId = static_cast<int64>(OwnerEntity.id());
		}
	}

	return UFlecsSpawnLibrary::SpawnProjectileFromEntityDef(
		GetWorld(),
		ProjectileDefinition,
		GetMuzzleLocation(),
		Direction,
		ProjectileSpeedOverride,
		OwnerEntityId
	);
}

TArray<FSkeletonKey> AFlecsCharacter::FireProjectileSpread(int32 Count, float SpreadAngle)
{
	TArray<FSkeletonKey> Keys;

	if (!ProjectileDefinition || !ProjectileDefinition->RenderProfile || !ProjectileDefinition->RenderProfile->Mesh)
	{
		return Keys;
	}

	if (!ProjectileDefinition->ProjectileProfile)
	{
		return Keys;
	}

	// Get owner entity ID for friendly fire prevention
	int64 OwnerEntityId = 0;
	if (UFlecsArtillerySubsystem* Subsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>())
	{
		flecs::entity OwnerEntity = Subsystem->GetEntityForBarrageKey(GetEntityKey());
		if (OwnerEntity.is_valid())
		{
			OwnerEntityId = static_cast<int64>(OwnerEntity.id());
		}
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

		FSkeletonKey Key = UFlecsSpawnLibrary::SpawnProjectileFromEntityDef(
			GetWorld(),
			ProjectileDefinition,
			SpawnLocation,
			SpreadRotation.Vector(),
			ProjectileSpeedOverride,
			OwnerEntityId
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
	// E closes loot panel if open
	if (IsLootOpen())
	{
		CloseLootPanel();
		return;
	}

	// If in an interaction state, route to state machine (E exits Focus, etc.)
	if (InteractionState != EInteractionState::Gameplay)
	{
		HandleInteractionInput();
		return;
	}

	// If we have an interaction target, begin interaction
	if (CurrentInteractionTarget.IsValid())
	{
		bInteractKeyHeld = true; // For Hold detection
		HandleInteractionInput();
		return;
	}

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
	else if (TestItemDefinition && TestItemDefinition->ItemDefinition && InventoryEntityId != 0)
	{
		// No test container — add item directly to player inventory
		int32 Added = 0;
		UFlecsContainerLibrary::AddItemToContainer(this, InventoryEntityId, TestItemDefinition, 1, Added, false); // bAutoStack = false for testing
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan,
				FString::Printf(TEXT("Added '%s' to inventory"), *TestItemDefinition->GetName()));
		}
	}
	else
	{
		// Fallback to entity spawning
		SpawnTestEntity();
	}
}

void AFlecsCharacter::OnInteractReleased(const FInputActionValue& Value)
{
	HandleInteractionRelease();
}

void AFlecsCharacter::OnInteractCancel(const FInputActionValue& Value)
{
	HandleInteractionCancel();
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
	// Resolve SkeletonKey → int64 for new container API
	int64 ContainerId = UFlecsEntityLibrary::GetEntityId(this, TestContainerKey);
	bool bSuccess = UFlecsContainerLibrary::AddItemToContainer(
		this,
		ContainerId,
		TestItemDefinition,
		1,  // Add 1 item
		ActuallyAdded,
		false  // bAutoStack = false for testing
	);

	// Get current count for display
	int32 ItemCount = UFlecsContainerLibrary::GetContainerItemCount(this, ContainerId);

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
	int64 ContainerId = UFlecsEntityLibrary::GetEntityId(this, TestContainerKey);
	int32 ItemCount = UFlecsContainerLibrary::GetContainerItemCount(this, ContainerId);

	// Remove all items
	UFlecsContainerLibrary::RemoveAllItemsFromContainer(this, ContainerId);

	FString Message = FString::Printf(TEXT("Removed all items from container (%d items removed)"), ItemCount);
	UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: %s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow, Message);
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// INTERACTION
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::PerformInteractionTrace()
{
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC) return;

	UBarrageDispatch* Barrage = GetWorld()->GetSubsystem<UBarrageDispatch>();
	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!Barrage || !FlecsSubsystem) return;

	// Get camera viewpoint
	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	FVector Direction = CameraRotation.Vector();

	// Set up filters: detect MOVING layer objects, ignore self
	auto BroadPhaseFilter = Barrage->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
	auto ObjectFilter = Barrage->GetDefaultLayerFilter(Layers::MOVING);
	FBarrageKey CharBarrageKey = Barrage->GetBarrageKeyFromSkeletonKey(CharacterKey);
	auto BodyFilter = Barrage->GetFilterToIgnoreSingleBody(CharBarrageKey);

	// Cast ray/sphere
	TSharedPtr<FHitResult> HitResult = MakeShared<FHitResult>();

	if (bUseSphereTrace)
	{
		Barrage->SphereCast(
			InteractionSphereRadius,
			MaxInteractionDistance,
			CameraLocation,
			Direction,
			HitResult,
			BroadPhaseFilter, ObjectFilter, BodyFilter);
	}
	else
	{
		Barrage->CastRay(
			CameraLocation,
			Direction * MaxInteractionDistance,
			BroadPhaseFilter, ObjectFilter, BodyFilter,
			HitResult);
	}

	FSkeletonKey NewTarget;

	if (HitResult->bBlockingHit)
	{
		// Convert BodyID → BarrageKey → SkeletonKey → Flecs entity
		FBarrageKey HitBarrageKey = Barrage->GetBarrageKeyFromFHitResult(HitResult);
		FBLet Prim = Barrage->GetShapeRef(HitBarrageKey);
		if (FBarragePrimitive::IsNotNull(Prim))
		{
			FSkeletonKey HitKey = Prim->KeyOutOfBarrage;
			flecs::entity HitEntity = FlecsSubsystem->GetEntityForBarrageKey(HitKey);

			if (HitEntity.is_valid() && HitEntity.has<FTagInteractable>() && !HitEntity.has<FTagDead>())
			{
				// Check interaction range from InteractionStatic
				const FInteractionStatic* InterStatic = HitEntity.try_get<FInteractionStatic>();
				float MaxRange = InterStatic ? InterStatic->MaxRange : MaxInteractionDistance;

				if (HitResult->Distance <= MaxRange)
				{
					NewTarget = HitKey;
				}
			}
		}
	}

	// Fire event if target changed
	if (NewTarget != CurrentInteractionTarget)
	{
		CurrentInteractionTarget = NewTarget;

		// Cache interaction prompt (avoids cross-thread reads later)
		if (NewTarget.IsValid())
		{
			flecs::entity TargetEntity = FlecsSubsystem->GetEntityForBarrageKey(NewTarget);
			if (TargetEntity.is_valid())
			{
				const FEntityDefinitionRef* DefRef = TargetEntity.try_get<FEntityDefinitionRef>();
				if (DefRef && DefRef->Definition && DefRef->Definition->InteractionProfile)
				{
					CachedInteractionPrompt = DefRef->Definition->InteractionProfile->InteractionPrompt;
					CachedInteractionType = DefRef->Definition->InteractionProfile->InteractionType;
					CachedHoldDuration = DefRef->Definition->InteractionProfile->HoldDuration;
				}
				else
				{
					CachedInteractionPrompt = NSLOCTEXT("Interaction", "Fallback", "Press E");
					CachedInteractionType = EInteractionType::Instant;
					CachedHoldDuration = 0.f;
				}
			}
			UE_LOG(LogTemp, Log, TEXT("Interaction: Target acquired Key=%llu"), static_cast<uint64>(NewTarget));
		}
		else
		{
			CachedInteractionPrompt = FText::GetEmpty();
			CachedInteractionType = EInteractionType::Instant;
			CachedHoldDuration = 0.f;

			// Auto-close loot panel when target lost (walked away)
			if (IsLootOpen())
			{
				CloseLootPanel();
			}
		}

		OnInteractionTargetChanged(NewTarget.IsValid(), NewTarget);

		// Broadcast to message system (game thread — direct broadcast)
		if (UFlecsMessageSubsystem* MsgSub = UFlecsMessageSubsystem::Get(this))
		{
			FUIInteractionMessage InterMsg;
			InterMsg.bHasTarget = NewTarget.IsValid();
			InterMsg.TargetKey = NewTarget;
			InterMsg.InteractionType = static_cast<uint8>(CachedInteractionType);
			InterMsg.HoldDuration = CachedHoldDuration;
			if (NewTarget.IsValid())
			{
				flecs::entity E = FlecsSubsystem->GetEntityForBarrageKey(NewTarget);
				InterMsg.EntityId = E.is_valid() ? static_cast<int64>(E.id()) : 0;
			}
			MsgSub->BroadcastMessage(TAG_UI_Interaction, InterMsg);
		}
	}
}

// TryInteract() removed — replaced by HandleInteractionInput() in FlecsCharacter_Interaction.cpp

FText AFlecsCharacter::GetInteractionPrompt() const
{
	return CachedInteractionPrompt;
}

// ═══════════════════════════════════════════════════════════════════════════
// IDENTITY
// ═══════════════════════════════════════════════════════════════════════════

FSkeletonKey AFlecsCharacter::GetEntityKey() const
{
	return CharacterKey;
}

// ═══════════════════════════════════════════════════════════════════════════
// WEAPON VISUAL
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::AttachWeaponVisual(USkeletalMesh* InMesh, const FTransform& AttachOffset)
{
	check(IsInGameThread());
	if (!InMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("AttachWeaponVisual: null mesh"));
		return;
	}
	check(WeaponMeshComponent);
	WeaponMeshComponent->SetSkeletalMesh(InMesh);
	WeaponMeshComponent->SetRelativeTransform(AttachOffset);
	WeaponMeshComponent->SetVisibility(true);
	UE_LOG(LogTemp, Log, TEXT("WEAPON VISUAL: Attached '%s'"), *InMesh->GetName());
}

void AFlecsCharacter::DetachWeaponVisual()
{
	check(IsInGameThread());
	if (!WeaponMeshComponent) return;
	WeaponMeshComponent->SetSkeletalMesh(nullptr);
	WeaponMeshComponent->SetVisibility(false);
}

// ═══════════════════════════════════════════════════════════════════════════
// WEAPON TESTING
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::StopFire(const FInputActionValue& Value)
{
	bPendingFireAfterSpawn = false;
	if (TestWeaponEntityId != 0)
	{
		StopFiringWeapon();
	}
}

void AFlecsCharacter::SpawnAndEquipTestWeapon()
{
	if (!TestWeaponDefinition || !TestWeaponDefinition->WeaponProfile)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter: TestWeaponDefinition is null or has no WeaponProfile!"));
		return;
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!FlecsSubsystem) return;

	// Get character's BarrageKey (from KeyCarry component)
	FSkeletonKey CharKey = GetEntityKey();
	if (!CharKey.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter: Character has no valid entity key!"));
		return;
	}

	// Capture visual data on game thread (UObject pointers not safe across threads).
	// Store in PendingWeaponEquip so Tick() can safely attach the visual.
	PendingWeaponEquip.Mesh = TestWeaponDefinition->WeaponProfile->EquippedMesh;
	PendingWeaponEquip.AttachOffset = TestWeaponDefinition->WeaponProfile->AttachOffset;

	// Create weapon entity via EnqueueCommand
	// IMPORTANT: Look up CharEntityId INSIDE the command, on simulation thread,
	// to ensure BeginPlay's entity binding has already been processed.
	FlecsSubsystem->EnqueueCommand([this, FlecsSubsystem, CharKey]()
	{
		flecs::world* World = FlecsSubsystem->GetFlecsWorld();
		if (!World) return;

		// Look up character entity on simulation thread (after BeginPlay binding)
		flecs::entity CharEntity = FlecsSubsystem->GetEntityForBarrageKey(CharKey);
		if (!CharEntity.is_valid())
		{
			UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter: No Flecs entity for character key %llu!"),
				static_cast<uint64>(CharKey));
			return;
		}
		int64 CharEntityId = static_cast<int64>(CharEntity.id());
		UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: Found character entity %lld for key %llu"),
			CharEntityId, static_cast<uint64>(CharKey));

		// Get or create prefab for weapon
		flecs::entity Prefab = FlecsSubsystem->GetOrCreateEntityPrefab(TestWeaponDefinition);
		if (!Prefab.is_valid())
		{
			UE_LOG(LogTemp, Warning, TEXT("FlecsCharacter: Failed to create weapon prefab!"));
			return;
		}

		// Create weapon entity from prefab
		flecs::entity WeaponEntity = World->entity()
			.is_a(Prefab)
			.add<FTagWeapon>();

		// Initialize FWeaponInstance
		const FWeaponStatic* Static = WeaponEntity.try_get<FWeaponStatic>();
		if (Static)
		{
			FWeaponInstance Instance;
			Instance.CurrentAmmo = Static->MagazineSize;
			Instance.ReserveAmmo = Static->MaxReserveAmmo;
			WeaponEntity.set<FWeaponInstance>(Instance);
		}

		// Equip to character
		FEquippedBy Equipped;
		Equipped.CharacterEntityId = CharEntityId;
		Equipped.SlotId = 0;
		WeaponEntity.set<FEquippedBy>(Equipped);

		// Store weapon entity ID (thread-safe assignment via main thread later)
		int64 WeaponId = static_cast<int64>(WeaponEntity.id());

		// Send initial ammo state to HUD (we're on sim thread, use EnqueueMessage)
		if (Static && UFlecsMessageSubsystem::SelfPtr)
		{
			FUIAmmoMessage AmmoMsg;
			AmmoMsg.WeaponEntityId = WeaponId;
			AmmoMsg.CurrentAmmo = Static->MagazineSize;
			AmmoMsg.MagazineSize = Static->MagazineSize;
			AmmoMsg.ReserveAmmo = Static->MaxReserveAmmo;
			UFlecsMessageSubsystem::SelfPtr->EnqueueMessage(TAG_UI_Ammo, AmmoMsg);
		}

		// Signal game thread via atomics (processed in Tick).
		// AsyncTask(GameThread) can execute during post-tick component update → crash.
		PendingWeaponEquip.WeaponId.store(WeaponId, std::memory_order_release);
		PendingWeaponEquip.bPending.store(true, std::memory_order_release);
	});
}

void AFlecsCharacter::StartFiringWeapon()
{
	if (TestWeaponEntityId == 0) return;

	// Update aim direction via lock-free bridge (ensures fresh data for first shot)
	if (auto* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>())
	{
		if (auto* Bridge = FlecsSubsystem->GetLateSyncBridge())
		{
			FAimDirection Aim;
			Aim.Direction = GetFiringDirection();
			Aim.CharacterPosition = FollowCamera ? FollowCamera->GetComponentLocation() : GetActorLocation();
			Aim.MuzzleWorldPosition = GetMuzzleLocation();
			Bridge->WriteAimDirection(GetCharacterEntityId(), Aim);
		}
	}

	// Start firing
	UFlecsWeaponLibrary::StartFiring(this, TestWeaponEntityId);
}

void AFlecsCharacter::StopFiringWeapon()
{
	if (TestWeaponEntityId == 0) return;
	UFlecsWeaponLibrary::StopFiring(this, TestWeaponEntityId);
}

void AFlecsCharacter::ReloadTestWeapon()
{
	if (TestWeaponEntityId == 0) return;
	UFlecsWeaponLibrary::ReloadWeapon(this, TestWeaponEntityId);
}

// ═══════════════════════════════════════════════════════════════════════════
// INVENTORY UI
// ═══════════════════════════════════════════════════════════════════════════

bool AFlecsCharacter::IsInventoryOpen() const
{
	return InventoryWidget && InventoryWidget->IsInventoryOpen();
}

bool AFlecsCharacter::IsLootOpen() const
{
	return LootPanel && LootPanel->IsLootOpen();
}

void AFlecsCharacter::ToggleInventory(const FInputActionValue& Value)
{
	// I key closes loot panel if open
	if (IsLootOpen())
	{
		CloseLootPanel();
		return;
	}

	if (!InventoryWidget || InventoryEntityId == 0) return;

	if (InventoryWidget->IsInventoryOpen())
	{
		InventoryWidget->CloseInventory();
		InventoryWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		InventoryWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		InventoryWidget->OpenInventory(InventoryEntityId);
	}
}

void AFlecsCharacter::OpenLootPanel(int64 ExternalContainerEntityId, const FText& ExternalTitle)
{
	if (!LootPanel || InventoryEntityId == 0) return;

	// Close existing loot if open
	if (IsLootOpen()) CloseLootPanel();

	// Close standalone inventory if open
	if (IsInventoryOpen())
	{
		InventoryWidget->CloseInventory();
		InventoryWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	LootPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	LootPanel->OpenLoot(InventoryEntityId, ExternalContainerEntityId, ExternalTitle);
}

void AFlecsCharacter::CloseLootPanel()
{
	if (!LootPanel || !LootPanel->IsLootOpen()) return;

	LootPanel->CloseLoot();
	LootPanel->SetVisibility(ESlateVisibility::Collapsed);
}


int64 AFlecsCharacter::GetCharacterEntityId() const
{
	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!FlecsSubsystem) return 0;

	FSkeletonKey CharKey = GetEntityKey();
	if (!CharKey.IsValid()) return 0;

	flecs::entity CharEntity = FlecsSubsystem->GetEntityForBarrageKey(CharKey);
	if (!CharEntity.is_valid()) return 0;

	return static_cast<int64>(CharEntity.id());
}
