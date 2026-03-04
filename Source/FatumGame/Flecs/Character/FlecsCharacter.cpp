// Core lifecycle, Tick, and identity for AFlecsCharacter.
// Other responsibilities split into:
//   FlecsCharacter_Input.cpp       — Input binding + all input handler methods
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
#include "FlecsMovementProfile.h"
#include "FlecsMovementStatic.h"
#include "EnhancedInputSubsystems.h"
#include "Components/SkeletalMeshComponent.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"
#include "FlecsHUDWidget.h"
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
	PosState.FeetToActorOffset = CapsuleHalfHeight; // init offset = standing HH

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

	ReadAndApplyBarragePosition(DeltaTime);  // 1. Jolt → lerp → SetActorLocation (before CameraManager)
	WriteCameraAtomics();                     // 2. Camera pos/dir → sim thread (blink, mantle)
	ConsumeTeleportSnap();                    // 3. Sim teleport → reset lerp buffers
	TickTimeDilation(DeltaTime);              // 4. Blink aim push/remove, stack tick, sim atomics
	TickPostureAndResnap(DeltaTime);          // 5. Posture effects, FeetToActorOffset re-snap
	CheckHealthChanges();                     // 6. Health change detection
	TickInteractionStateMachine(DeltaTime);   // 7. Focus/Hold state machine
	UpdateCamera();                           // 8. FP position + rotation + FOV
	SyncMovementStateToECS();                 // 9. Posture → Flecs (on change)
	ProcessPendingWeaponEquip();              // 10. Sim→game weapon attach
	WriteAimDirection();                      // 11. LateSyncBridge FAimDirection
}

// ═══════════════════════════════════════════════════════════════════════════
// TICK HELPERS
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::WriteCameraAtomics()
{
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
}

void AFlecsCharacter::ConsumeTeleportSnap()
{
	if (StateAtomics && StateAtomics->Teleported.Consume())
	{
		PosState.bJustSpawned = true; // Next ReadAndApplyBarragePosition will snap Prev=Curr=Smoothed
	}
}

void AFlecsCharacter::TickTimeDilation(float DeltaTime)
{
	// Push/remove blink aim source based on BlinkAiming state.
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
			DilationStack.Remove(BlinkAimTag);
		}
		bPrevBlinkAiming = bAiming;
	}

	// Tick dilation stack → write to sim thread + UE GlobalTimeDilation.
	// Use undilated wall-clock DT for stack tick (durations count in real time).
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

		float PublishedScale = Sub->GetSimWorker().ActiveTimeScalePublished.load(std::memory_order_relaxed);
		UGameplayStatics::SetGlobalTimeDilation(GetWorld(), PublishedScale);
	}
	else if (UWorld* W = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(W, TargetScale);
	}
}

void AFlecsCharacter::TickPostureAndResnap(float DeltaTime)
{
	if (!FatumMovement || !StateAtomics) return;

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
		if (!FMath::IsNearlyEqual(PosState.FeetToActorOffset, CurrentHH, 0.01f))
		{
			PosState.FeetToActorOffset = CurrentHH;
			SetActorLocation(PosState.SmoothedPos + FVector(0, 0, PosState.FeetToActorOffset),
				false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
}

void AFlecsCharacter::UpdateCamera()
{
	// Skip when Focus interaction manually drives camera position/rotation.
	bool bFocusDrivingCamera = (Interact.State == EInteractionState::Focusing
		|| Interact.State == EInteractionState::Unfocusing
		|| (Interact.State == EInteractionState::Focused
			&& Interact.ActiveProfile && Interact.ActiveProfile->bMoveCamera));

	if (!FatumMovement || !FollowCamera || bFocusDrivingCamera) return;

	FollowCamera->SetFieldOfView(BaseFOV + FatumMovement->GetCurrentFOVOffset());

	if (bFirstPersonCamera)
	{
		FVector CameraPos(
			0.f,
			FatumMovement->GetHeadBobHorizontalOffset(),
			FatumMovement->GetCurrentEyeHeight()
				+ FatumMovement->GetLandingCameraOffset()
				+ FatumMovement->GetHeadBobVerticalOffset());
		FollowCamera->SetRelativeLocation(CameraPos);

		FRotator ControlRot = GetControlRotation();
		FollowCamera->SetWorldRotation(
			FRotator(ControlRot.Pitch, ControlRot.Yaw, FatumMovement->GetSlideTiltAngle()));
	}
}

void AFlecsCharacter::ProcessPendingWeaponEquip()
{
	if (!PendingWeaponEquip.bPending.load(std::memory_order_acquire)) return;

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

void AFlecsCharacter::WriteAimDirection()
{
	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();

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

// Input binding + handler methods: see FlecsCharacter_Input.cpp
