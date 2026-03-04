// Core lifecycle, input routing, and identity for AFlecsCharacter.
// Other responsibilities split into:
//   FlecsCharacter_Physics.cpp     — Barrage position readback, CMC feed, posture→Jolt
//   FlecsCharacter_Combat.cpp      — Health, projectile, weapon
//   FlecsCharacter_Interaction.cpp — Detection + state machine
//   FlecsCharacter_UI.cpp          — HUD, inventory, loot panels
//   FlecsCharacter_Test.cpp        — Dev scaffolding (entity/container testing)

#include "FlecsCharacter.h"
#include "FatumMovementComponent.h"
#include "FlecsMovementComponents.h"
#include "FlecsEntityDefinition.h"
#include "FlecsInteractionProfile.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsStaticComponents.h"
#include "FlecsInstanceComponents.h"
#include "FlecsContainerLibrary.h"
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
#include "FatumInputComponent.h"
#include "FatumInputTags.h"
#include "FlecsMovementProfile.h"
#include "FlecsMovementStatic.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Components/SkeletalMeshComponent.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"
#include "FlecsHUDWidget.h"
#include "FlecsItemDefinition.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformTime.h"
#include "FlecsAbilityTypes.h"
#include "FlecsAbilityStates.h"
#include "FlecsAbilityDefinition.h"
#include "FlecsAbilityLoadout.h"

// ═══════════════════════════════════════════════════════════════════════════
// CONSTRUCTOR
// ═══════════════════════════════════════════════════════════════════════════

AFlecsCharacter::AFlecsCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UFatumMovementComponent>(
		ACharacter::CharacterMovementComponentName))
{
	FatumMovement = Cast<UFatumMovementComponent>(GetCharacterMovement());

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

// ═══════════════════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Identity: generate key from actor pointer hash (replaces UPlayerKeyCarry)
	CharacterKey = MAKE_ACTORKEY(this);

	InitCamera();
	InitECSRegistration();
	InitInventoryContainers();  // _UI.cpp

	// Enhanced Input: add mapping context
	if (APlayerController* PC = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (GameplayMappingContext)
			{
				Subsystem->AddMappingContext(GameplayMappingContext, 0);
			}
		}
	}

	// Subscribe to posture changes for Barrage shape sync
	if (FatumMovement)
	{
		FatumMovement->OnPostureChanged.AddUObject(this, &AFlecsCharacter::HandlePostureChanged);
	}

	// Auto-equip weapon if TestWeaponDefinition is set
	if (TestWeaponDefinition && TestWeaponDefinition->WeaponProfile)
	{
		SpawnAndEquipTestWeapon();
	}

	InitInteractionTrace();  // _Interaction.cpp
	InitUI();                // _UI.cpp
}

void AFlecsCharacter::InitCamera()
{
	if (bFirstPersonCamera)
	{
		// First-person: camera at eye level, no boom
		CameraBoom->SetActive(false);
		CameraBoom->SetVisibility(false);
		FollowCamera->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		float EyeH = FatumMovement ? FatumMovement->GetCurrentEyeHeight() : 60.f;
		FollowCamera->SetRelativeLocation(FVector(0.f, 0.f, EyeH));
		FollowCamera->bUsePawnControlRotation = false; // Manual rotation for additive Roll (tilt/lean)
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
}

void AFlecsCharacter::InitECSRegistration()
{
	float InitialHealth = StartingHealth > 0.f ? StartingHealth : MaxHealth;
	CachedHealth = InitialHealth;

	FSkeletonKey Key = CharacterKey;
	if (!Key.IsValid()) return;

	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!FlecsSubsystem) return;

	float CapturedMaxHealth = MaxHealth;
	float CapturedInitialHealth = InitialHealth;
	float CapturedArmor = Armor;
	FVector SpawnLocation = GetActorLocation();
	float CapsuleRadius = GetCapsuleComponent()->GetScaledCapsuleRadius();
	float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	// Allocate shared atomics on game thread — bridge will hold second refs.
	InputAtomics = MakeShared<FCharacterInputAtomics, ESPMode::ThreadSafe>();
	StateAtomics = MakeShared<FCharacterStateAtomics, ESPMode::ThreadSafe>();
	FeetToActorOffset = CapsuleHalfHeight; // init offset = standing HH

	// Capture movement profile values for sim thread (can't access UObjects there).
	float CapturedGravityScale = 1.f;
	FMovementStatic CapturedMS;
	if (FatumMovement && FatumMovement->MovementProfile)
	{
		CapturedMS = FMovementStatic::FromProfile(FatumMovement->MovementProfile);
		CapturedGravityScale = CapturedMS.GravityScale;
	}

	// Capture ability loadout → FAbilitySystem (game thread, can access UObjects).
	FAbilitySystem CapturedAbilSys;
	checkf(AbilityLoadout, TEXT("AFlecsCharacter: AbilityLoadout (Flecs|Abilities) must be set on %s"), *GetName());
	{
		int32 Count = FMath::Min(AbilityLoadout->Abilities.Num(), MAX_ABILITY_SLOTS);
		for (int32 i = 0; i < Count; ++i)
		{
			const UFlecsAbilityDefinition* Def = AbilityLoadout->Abilities[i];
			if (!Def || Def->AbilityType == EAbilityType::None) continue;

			FAbilitySlot& Slot = CapturedAbilSys.Slots[CapturedAbilSys.SlotCount];
			Slot.TypeId = static_cast<EAbilityTypeId>(Def->AbilityType);
			Slot.MaxCharges = static_cast<int8>(Def->MaxCharges);
			Slot.Charges = static_cast<int8>(Def->StartingCharges); // -1 = infinite
			Slot.RechargeRate = Def->RechargeRate;
			Slot.CooldownDuration = Def->CooldownDuration;
			Slot.bAlwaysTick = Def->bAlwaysTick;
			Slot.bExclusive = Def->bExclusive;

			// Copy per-ability-type config into slot buffer
			if (Def->AbilityType == EAbilityType::KineticBlast)
			{
				FMemory::Memcpy(Slot.ConfigData, &Def->KineticBlastConfig, sizeof(FKineticBlastConfig));
			}

			CapturedAbilSys.SlotCount++;
		}
	}

	TWeakObjectPtr<AFlecsCharacter> WeakSelf(this);
	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Key, CapturedMaxHealth, CapturedInitialHealth,
		CapturedArmor, SpawnLocation, CapsuleRadius, CapsuleHalfHeight, WeakSelf,
		CapturedGravityScale, CapturedMS, CapturedAbilSys]()
	{
		flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		// Create Barrage CharacterVirtual for collision-resolved movement.
		UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
		if (Physics)
		{
			FBCharParams CharParams;
			// Send FEET position to Jolt, not capsule center.
			CharParams.point = SpawnLocation - FVector(0, 0, CapsuleHalfHeight);
			CharParams.JoltHalfHeightOfCylinder = FMath::Max((CapsuleHalfHeight - CapsuleRadius) / 100.0, 0.01);
			CharParams.JoltRadius = CapsuleRadius / 100.0;
			CharParams.speed = 5000.0;

			FBLet Body = Physics->CreatePrimitive(
				CharParams, Key,
				static_cast<uint16>(EPhysicsLayer::MOVING));

			if (FBarragePrimitive::IsNotNull(Body))
			{
				FBarragePrimitive::Apply_Unsafe(
					FQuat4d(1, 1, 1, 1), Body, PhysicsInputType::Throttle);

				FBarragePrimitive::Apply_Unsafe(
					FQuat4d(0, -980.0 * CapturedGravityScale, 0, 0),
					Body, PhysicsInputType::SetCharacterGravity);

				if (WeakSelf.IsValid())
				{
					WeakSelf->CachedBarrageBody = Body;
				}
			}
		}

		// Static + Instance health data
		FHealthStatic HealthStatic;
		HealthStatic.MaxHP = CapturedMaxHealth;
		HealthStatic.Armor = CapturedArmor;
		HealthStatic.RegenPerSecond = 0.f;
		HealthStatic.bDestroyOnDeath = false;

		FHealthInstance HealthInstance;
		HealthInstance.CurrentHP = CapturedInitialHealth;
		HealthInstance.RegenAccumulator = 0.f;

		flecs::entity Entity = FlecsWorld->entity()
			.set<FHealthStatic>(HealthStatic)
			.set<FHealthInstance>(HealthInstance)
			.add<FTagCharacter>();

		// Bidirectional binding: sets FBarrageBody + atomic in FBarragePrimitive
		FlecsSubsystem->BindEntityToBarrage(Entity, Key);

		// Movement state: AnimBP sync (cosmetic) + sim thread authority (FCharacterMoveState)
		FMovementState InitState;
		Entity.set<FMovementState>(InitState);

		FCharacterMoveState InitMoveState;
		Entity.set<FCharacterMoveState>(InitMoveState);

		// Movement parameters for PrepareCharacterStep (speeds, accel, capsule)
		Entity.set<FMovementStatic>(CapturedMS);

		// Sim-thread state for ability FSMs (blink charges, jump buffer, coyote, etc.)
		FCharacterSimState SimState;
		Entity.set<FCharacterSimState>(SimState);

		// Ability system (generic slots + per-ability state components)
		Entity.set<FAbilitySystem>(CapturedAbilSys);
		FSlideState InitSlide;
		Entity.set<FSlideState>(InitSlide);
		FBlinkState InitBlink;
		Entity.set<FBlinkState>(InitBlink);
		FMantleState InitMantle;
		Entity.set<FMantleState>(InitMantle);

		// Register in sim→game state cache with initial health
		FlecsSubsystem->GetSimStateCache().Register(static_cast<int64>(Entity.id()));
		FlecsSubsystem->GetSimStateCache().WriteHealth(
			static_cast<int64>(Entity.id()), CapturedInitialHealth, CapturedMaxHealth);

		// Register for sim-thread position readback
		if (WeakSelf.IsValid())
		{
			FlecsSubsystem->RegisterCharacterBridge(WeakSelf.Get());
		}

		// Send initial health to HUD
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

	UFlecsArtillerySubsystem::RegisterLocalPlayer(this, Key);
}

void AFlecsCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CleanupUI();           // _UI.cpp
	CleanupInteraction();  // _Interaction.cpp
	DetachWeaponVisual();  // _Combat.cpp
	UnregisterFromECS();

	Super::EndPlay(EndPlayReason);
}

void AFlecsCharacter::UnregisterFromECS()
{
	UFlecsArtillerySubsystem::UnregisterLocalPlayer();

	FSkeletonKey Key = GetEntityKey();
	if (!Key.IsValid()) return;

	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!FlecsSubsystem) return;

	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Key]()
	{
		FlecsSubsystem->UnregisterCharacterBridge(Key);

		flecs::entity Entity = FlecsSubsystem->GetEntityForBarrageKey(Key);
		if (Entity.is_valid() && Entity.is_alive())
		{
			FlecsSubsystem->UnbindEntityFromBarrage(Entity);
			Entity.destruct();
		}
	});
}

// ═══════════════════════════════════════════════════════════════════════════
// TICK
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 1. Read Jolt position, feed CMC, set actor location.
	//    Must happen in TG_PrePhysics — BEFORE CameraManager (TG_PostPhysics).
	ReadAndApplyBarragePosition(DeltaTime);

	// 1.5. Write camera pos/dir for sim thread (blink targeting + mantle hang exit).
	if (InputAtomics.IsValid() && FollowCamera)
	{
		FVector CamLoc = FollowCamera->GetComponentLocation();
		FVector CamDir = FollowCamera->GetForwardVector();
		InputAtomics->CamLocX.Write(static_cast<float>(CamLoc.X));
		InputAtomics->CamLocY.Write(static_cast<float>(CamLoc.Y));
		InputAtomics->CamLocZ.Write(static_cast<float>(CamLoc.Z));
		InputAtomics->CamDirX.Write(static_cast<float>(CamDir.X));
		InputAtomics->CamDirY.Write(static_cast<float>(CamDir.Y));
		InputAtomics->CamDirZ.Write(static_cast<float>(CamDir.Z));
	}

	// 1.6. Teleport snap: sim thread teleported character → snap interpolation buffers.
	if (StateAtomics && StateAtomics->Teleported.Consume())
	{
		bBarrageJustSpawned = true; // Next ReadAndApplyBarragePosition will snap Prev=Curr=Smoothed
	}

	// 1.7. Time dilation: push/remove blink aim source based on BlinkAiming state.
	static const FName BlinkAimTag("BlinkAim");
	if (StateAtomics)
	{
		bool bAiming = StateAtomics->BlinkAiming.Read();
		if (bAiming && !bPrevBlinkAiming)
		{
			FDilationEntry Entry;
			Entry.Tag = BlinkAimTag;
			Entry.DesiredScale = (FatumMovement && FatumMovement->MovementProfile)
				? FatumMovement->MovementProfile->BlinkAimTimeDilation : 0.3f;
			Entry.bPlayerFullSpeed = true;
			Entry.EntrySpeed = 20.f;
			Entry.ExitSpeed = 10.f;
			DilationStack.Push(Entry);
		}
		else if (!bAiming && bPrevBlinkAiming)
		{
			DilationStack.Remove(BlinkAimTag); // auto-captures ExitSpeed
		}
		bPrevBlinkAiming = bAiming;
	}

	// Tick dilation stack → write to sim thread + UE GlobalTimeDilation.
	{
		// Use undilated wall-clock DT for stack tick (durations count in real time).
		// FApp::GetDeltaTime() and Tick DeltaTime are both dilated by GlobalTimeDilation.
		double Now = FPlatformTime::Seconds();
		float RealDT = (LastRealTickTime > 0.0) ? FMath::Clamp(static_cast<float>(Now - LastRealTickTime), 0.0001f, 0.1f) : DeltaTime;
		LastRealTickTime = Now;
		DilationStack.Tick(RealDT);

		float TargetScale = DilationStack.GetTargetScale();
		float InterpSpeed = DilationStack.GetTransitionSpeed();

		if (auto* Sub = GetWorld() ? GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>() : nullptr)
		{
			Sub->GetSimWorker().DesiredTimeScale.store(TargetScale, std::memory_order_relaxed);
			Sub->GetSimWorker().bPlayerFullSpeed.store(
				DilationStack.IsPlayerFullSpeed(), std::memory_order_relaxed);
			Sub->GetSimWorker().TransitionSpeed.store(InterpSpeed, std::memory_order_relaxed);

			// Mirror sim thread's smoothed scale for UE GlobalTimeDilation (prevents snap vs smooth desync)
			float PublishedScale = Sub->GetSimWorker().ActiveTimeScalePublished.load(std::memory_order_relaxed);
			UGameplayStatics::SetGlobalTimeDilation(GetWorld(), PublishedScale);
		}
		else if (UWorld* W = GetWorld())
		{
			UGameplayStatics::SetGlobalTimeDilation(W, TargetScale);
		}
	}

	// 2. Posture/abilities/camera effects — uses CURRENT velocity/GS from step 1.
	//    Must run BEFORE camera update so eye height is fresh (not 1 frame stale).
	if (FatumMovement && StateAtomics)
	{
		FatumMovement->TickPostureAndEffects(DeltaTime,
			StateAtomics->SlideActive.Read(),
			StateAtomics->MantleActive.Read(),
			StateAtomics->Hanging.Read(),
			StateAtomics->MantleType.Read());

		// If posture changed capsule while grounded, re-snap FeetToActorOffset
		// and re-set actor location (step 1 used pre-posture capsule HH).
		if (FatumMovement->IsMovingOnGround())
		{
			float CurrentHH = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			if (!FMath::IsNearlyEqual(FeetToActorOffset, CurrentHH, 0.01f))
			{
				FeetToActorOffset = CurrentHH;
				SetActorLocation(SmoothedBarragePos + FVector(0, 0, FeetToActorOffset),
					false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
	}

	CheckHealthChanges();
	TickInteractionStateMachine(DeltaTime);

	// 3. Camera update — reads CURRENT eye height (fresh from step 2).
	//    Skip when Focus interaction manually drives camera position/rotation.
	bool bFocusDrivingCamera = (InteractionState == EInteractionState::Focusing
		|| InteractionState == EInteractionState::Unfocusing
		|| (InteractionState == EInteractionState::Focused
			&& ActiveInteractionProfile && ActiveInteractionProfile->bMoveCamera));

	if (FatumMovement && FollowCamera && !bFocusDrivingCamera)
	{
		FollowCamera->SetFieldOfView(BaseFOV + FatumMovement->GetCurrentFOVOffset());

		if (bFirstPersonCamera)
		{
			// Position: eye height + landing compress + head bob (Y = lateral sway, Z = vertical)
			FVector CameraPos(
				0.f,
				FatumMovement->GetHeadBobHorizontalOffset(),
				FatumMovement->GetCurrentEyeHeight()
					+ FatumMovement->GetLandingCameraOffset()
					+ FatumMovement->GetHeadBobVerticalOffset());
			FollowCamera->SetRelativeLocation(CameraPos);

			// Rotation: ControlRotation + additive Roll (slide tilt, future: lean)
			FRotator ControlRot = GetControlRotation();
			FollowCamera->SetWorldRotation(
				FRotator(ControlRot.Pitch, ControlRot.Yaw, FatumMovement->GetSlideTiltAngle()));
		}
	}

	SyncMovementStateToECS();

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

	// Continuously update aim direction, muzzle offset, and position so WeaponFireSystem has fresh data.
	int64 CharId = GetCharacterEntityId();
	if (CharId != 0 && FlecsSubsystem)
	{
		if (auto* Bridge = FlecsSubsystem->GetLateSyncBridge())
		{
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
// IDENTITY
// ═══════════════════════════════════════════════════════════════════════════

FSkeletonKey AFlecsCharacter::GetEntityKey() const
{
	return CharacterKey;
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

// ═══════════════════════════════════════════════════════════════════════════
// ENHANCED INPUT SETUP
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UFatumInputComponent* FatumInput = CastChecked<UFatumInputComponent>(PlayerInputComponent);
	checkf(InputConfig, TEXT("AFlecsCharacter: InputConfig is not set! Assign a UFatumInputConfig Data Asset."));

	FatumInput->BindNativeAction(InputConfig, TAG_Input_Move,      ETriggerEvent::Triggered, this, &AFlecsCharacter::Move);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Move,      ETriggerEvent::Completed, this, &AFlecsCharacter::Move);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Look,      ETriggerEvent::Triggered, this, &AFlecsCharacter::Look);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Jump,      ETriggerEvent::Started,   this, &AFlecsCharacter::OnJumpStarted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Jump,      ETriggerEvent::Completed, this, &AFlecsCharacter::OnJumpCompleted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Fire,      ETriggerEvent::Started,   this, &AFlecsCharacter::StartFire);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Fire,      ETriggerEvent::Completed, this, &AFlecsCharacter::StopFire);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Interact,  ETriggerEvent::Started,   this, &AFlecsCharacter::OnSpawnItem);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Interact,  ETriggerEvent::Completed, this, &AFlecsCharacter::OnInteractReleased);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Destroy,   ETriggerEvent::Started,   this, &AFlecsCharacter::OnDestroyItem);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Inventory, ETriggerEvent::Started,   this, &AFlecsCharacter::ToggleInventory);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Cancel,    ETriggerEvent::Started,   this, &AFlecsCharacter::OnInteractCancel);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Sprint,    ETriggerEvent::Started,   this, &AFlecsCharacter::OnSprintStarted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Sprint,    ETriggerEvent::Completed, this, &AFlecsCharacter::OnSprintCompleted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Crouch,    ETriggerEvent::Started,   this, &AFlecsCharacter::OnCrouchStarted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Crouch,    ETriggerEvent::Completed, this, &AFlecsCharacter::OnCrouchCompleted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Prone,     ETriggerEvent::Started,   this, &AFlecsCharacter::OnProneStarted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Prone,     ETriggerEvent::Completed, this, &AFlecsCharacter::OnProneCompleted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Ability1,  ETriggerEvent::Started,   this, &AFlecsCharacter::OnAbility1Started);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Ability1,  ETriggerEvent::Completed, this, &AFlecsCharacter::OnAbility1Completed);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Ability2,  ETriggerEvent::Started,   this, &AFlecsCharacter::OnAbility2Started);
}

UInputComponent* AFlecsCharacter::CreatePlayerInputComponent()
{
	return NewObject<UFatumInputComponent>(this, UFatumInputComponent::StaticClass(), TEXT("FatumInputComponent"));
}

// ═══════════════════════════════════════════════════════════════════════════
// INPUT HANDLERS (thin routing — delegates to CMC / Combat / Interaction / Test)
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::Move(const FInputActionValue& Value)
{
	// Input is a Vector2D (X = Right/Left, Y = Forward/Back)
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr && InputAtomics.IsValid())
	{
		// Get controller rotation and extract yaw only
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// Calculate forward and right directions
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// Build world-space direction (not normalized — magnitude = stick tilt)
		FVector WorldDir = ForwardDirection * MovementVector.Y + RightDirection * MovementVector.X;

		// Write to atomics (lock-free, latest-wins) — sim thread reads in PrepareCharacterStep.
		InputAtomics->DirX.Write(static_cast<float>(WorldDir.X));
		InputAtomics->DirZ.Write(static_cast<float>(WorldDir.Y));
	}
}

void AFlecsCharacter::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

void AFlecsCharacter::OnSprintStarted(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->Sprinting.Write(true);
	if (FatumMovement) FatumMovement->RequestSprint(true); // game-thread cosmetics (FOV, move mode)
}

void AFlecsCharacter::OnSprintCompleted(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->Sprinting.Write(false);
	if (FatumMovement) FatumMovement->RequestSprint(false);
}

void AFlecsCharacter::OnJumpStarted(const FInputActionValue& Value)
{
	// One-shot: sim thread reads + clears in PrepareCharacterStep
	if (InputAtomics) InputAtomics->JumpPressed.Fire();
}

void AFlecsCharacter::OnJumpCompleted(const FInputActionValue& Value)
{
	// Jump is one-shot (consumed by sim thread), no release needed
}

void AFlecsCharacter::OnCrouchStarted(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->CrouchHeld.Write(true);
	if (FatumMovement) FatumMovement->RequestCrouch(true); // game-thread PostureSM (non-slide crouch)
}

void AFlecsCharacter::OnCrouchCompleted(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->CrouchHeld.Write(false);
	if (FatumMovement) FatumMovement->RequestCrouch(false);
}

void AFlecsCharacter::OnProneStarted(const FInputActionValue& Value)
{
	if (FatumMovement) FatumMovement->RequestProne(true);
}

void AFlecsCharacter::OnProneCompleted(const FInputActionValue& Value)
{
	if (FatumMovement) FatumMovement->RequestProne(false);
}

void AFlecsCharacter::OnAbility1Started(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->BlinkHeld.Write(true);
}

void AFlecsCharacter::OnAbility1Completed(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->BlinkHeld.Write(false);
}

void AFlecsCharacter::OnAbility2Started(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->Ability2Pressed.Fire();
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

void AFlecsCharacter::StopFire(const FInputActionValue& Value)
{
	bPendingFireAfterSpawn = false;
	if (TestWeaponEntityId != 0)
	{
		StopFiringWeapon();
	}
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
		UFlecsContainerLibrary::AddItemToContainer(this, InventoryEntityId, TestItemDefinition, 1, Added, false);
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
