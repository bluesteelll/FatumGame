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
#include "FlecsBarrageComponents.h"
#include "FlecsHealthComponents.h"
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
#include "FlecsAbilityStates.h"
#include "FlecsResourceTypes.h"
#include "FlecsResourcePoolProfile.h"
#include "FlecsHealthProfile.h"
#include "FlecsSwingableComponents.h"
#include "FRopeVisualRenderer.h"
#include "FlecsStealthComponents.h"
#include "FlecsWeaponProfile.h"
#include "FlecsVitalsComponents.h"
#include "FlecsVitalsProfile.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Debug/DebugDrawService.h"

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

AFlecsCharacter::~AFlecsCharacter()
{
	delete RopeRenderer;
	RopeRenderer = nullptr;
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

#if !UE_BUILD_SHIPPING
	InertiaDebugDrawHandle = UDebugDrawService::Register(
		TEXT("Game"),
		FDebugDrawDelegate::CreateUObject(this, &AFlecsCharacter::DrawInertiaDebug));
#endif
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
	checkf(CharacterDefinition, TEXT("CharacterDefinition must be set on %s"), *GetName());
	check(CharacterKey.IsValid());

	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	check(FlecsSubsystem);

	// Allocate shared atomics (game thread ownership, shared with bridge).
	InputAtomics = MakeShared<FCharacterInputAtomics, ESPMode::ThreadSafe>();
	StateAtomics = MakeShared<FCharacterStateAtomics, ESPMode::ThreadSafe>();
	RopeVisualAtomics = MakeShared<FRopeVisualAtomics>();
	RopeRenderer = new FRopeVisualRenderer();
	RopeRenderer->Activate(GetRootComponent(), RopeVisualAtomics.Get());
	PosState.FeetToActorOffset = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	// Cache initial health for game-thread change detection.
	if (CharacterDefinition->HealthProfile)
		CachedHealth = CharacterDefinition->HealthProfile->GetStartingHealth();

	// Cache resource static data for game-thread UI polling.
	if (CharacterDefinition->ResourcePoolProfile)
	{
		FResourcePools TempPools = FResourcePools::FromProfile(CharacterDefinition->ResourcePoolProfile);
		CachedResourcePoolCount = TempPools.PoolCount;
		for (int32 p = 0; p < TempPools.PoolCount; ++p)
		{
			ResourcePoolMaxValues[p] = TempPools.Pools[p].MaxValue;
			ResourcePoolTypes[p] = static_cast<uint8>(TempPools.Pools[p].TypeId);
		}
	}

	// Cache vitals flag for game-thread UI polling.
	bHasVitals = CharacterDefinition->VitalsProfile != nullptr;

	// Capture actor-specific data for sim thread (POD only).
	const FSkeletonKey Key = CharacterKey;
	const FVector SpawnLoc = GetActorLocation();
	const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	UFlecsEntityDefinition* Definition = CharacterDefinition;

	// Resolve gravity scale from MovementProfile.
	float GravityScale = 1.f;
	UFlecsMovementProfile* MoveProf = Definition->MovementProfile
		? Definition->MovementProfile
		: (FatumMovement ? FatumMovement->MovementProfile : nullptr);
	if (MoveProf)
		GravityScale = MoveProf->GravityScale;

	TWeakObjectPtr<AFlecsCharacter> WeakSelf(this);

	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Key, SpawnLoc, Radius, HalfHeight,
	                                 GravityScale, Definition, WeakSelf]()
	{
		flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
		check(FlecsWorld);

		// ── Barrage CharacterVirtual ──
		UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
		check(Physics);
		{
			FBCharParams CharParams;
			CharParams.point = SpawnLoc - FVector(0, 0, HalfHeight);
			CharParams.JoltHalfHeightOfCylinder = FMath::Max(
				(HalfHeight - Radius) / 100.0, 0.01);
			CharParams.JoltRadius = Radius / 100.0;
			CharParams.speed = 5000.0;

			FBLet Body = Physics->CreatePrimitive(
				CharParams, Key, static_cast<uint16>(EPhysicsLayer::MOVING));

			if (FBarragePrimitive::IsNotNull(Body))
			{
				FBarragePrimitive::Apply_Unsafe(
					FQuat4d(1, 1, 1, 1), Body, PhysicsInputType::Throttle);
				FBarragePrimitive::Apply_Unsafe(
					FQuat4d(0, -980.0 * GravityScale, 0, 0),
					Body, PhysicsInputType::SetCharacterGravity);
				if (WeakSelf.IsValid())
					WeakSelf->CachedBarrageBody = Body;
			}
		}

		// ── Flecs entity with prefab inheritance ──
		// is_a(Prefab) inherits ALL components: FHealthStatic, FHealthInstance,
		// FAbilitySystem, FResourcePools, FMovementStatic, etc.
		// Flecs copy-on-write: first get_mut<T>() creates per-entity mutable copy.
		flecs::entity Prefab = FlecsSubsystem->GetOrCreateEntityPrefab(Definition);
		checkf(Prefab.is_valid(), TEXT("Failed to create prefab for %s"), *Definition->GetName());

		FlecsWorld->defer_begin();

		flecs::entity Entity = FlecsWorld->entity()
			.is_a(Prefab)
			.add<FTagCharacter>();

		FlecsSubsystem->BindEntityToBarrage(Entity, Key);

		// Character-only instance components (not on generic entity prefabs).
		Entity.set<FMovementState>(FMovementState{});
		Entity.set<FCharacterMoveState>(FCharacterMoveState{});
		Entity.set<FCharacterSimState>(FCharacterSimState{});
		Entity.set<FSlideState>(FSlideState{});
		Entity.set<FBlinkState>(FBlinkState{});
		Entity.set<FMantleState>(FMantleState{});
		{ FTelekinesisState TKState; Entity.set<FTelekinesisState>(TKState); }
		Entity.set<FClimbState>(FClimbState{});
		Entity.set<FRopeSwingState>(FRopeSwingState{});
		Entity.set<FStealthInstance>(FStealthInstance{});

		// Vitals: per-entity instance components (FVitalsInstance/FVitalsStatic inherited from prefab)
		if (Definition->VitalsProfile)
		{
			Entity.set<FStatModifiers>(FStatModifiers{});
			Entity.set<FEquipmentVitalsCache>(FEquipmentVitalsCache{});
			FCharacterInventoryRef InvRef;
			// InventoryEntityId set later by InitInventoryContainers
			Entity.set<FCharacterInventoryRef>(InvRef);
		}

		FlecsWorld->defer_end();

		// SimStateCache
		const int64 EntityId = static_cast<int64>(Entity.id());
		checkf(Definition->HealthProfile, TEXT("CharacterDefinition '%s' must have HealthProfile"), *Definition->GetName());
		const float InitHP = Definition->HealthProfile->GetStartingHealth();
		const float MaxHP = Definition->HealthProfile->MaxHealth;
		FlecsSubsystem->GetSimStateCache().Register(EntityId);
		FlecsSubsystem->GetSimStateCache().WriteHealth(EntityId, InitHP, MaxHP);

		if (Definition->ResourcePoolProfile)
		{
			const FResourcePools* Res = Entity.try_get<FResourcePools>();
			if (Res && Res->PoolCount > 0)
			{
				float Ratios[4] = {};
				for (int32 p = 0; p < Res->PoolCount; ++p)
					Ratios[p] = Res->Pools[p].GetRatio();
				FlecsSubsystem->GetSimStateCache().WriteResources(EntityId, Ratios, Res->PoolCount);
			}
		}

		if (Definition->VitalsProfile)
		{
			FlecsSubsystem->GetSimStateCache().WriteVitals(EntityId,
				Definition->VitalsProfile->StartingHunger,
				Definition->VitalsProfile->StartingThirst,
				Definition->VitalsProfile->StartingWarmth);
		}

		// Bridge registration
		if (WeakSelf.IsValid())
			FlecsSubsystem->RegisterCharacterBridge(WeakSelf.Get());

		// Initial HUD health message
		if (UFlecsMessageSubsystem::SelfPtr)
		{
			FUIHealthMessage Msg;
			Msg.EntityId = EntityId;
			Msg.CurrentHP = InitHP;
			Msg.MaxHP = MaxHP;
			UFlecsMessageSubsystem::SelfPtr->EnqueueMessage(TAG_UI_Health, Msg);
		}

		UE_LOG(LogTemp, Log, TEXT("FlecsCharacter: Registered with prefab '%s', Key=%llu, HP=%.0f/%.0f"),
			*Definition->GetName(), static_cast<uint64>(Key), InitHP, MaxHP);
	});

	UFlecsArtillerySubsystem::RegisterLocalPlayer(this, CharacterKey);
}

void AFlecsCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if !UE_BUILD_SHIPPING
	if (InertiaDebugDrawHandle.IsValid())
	{
		UDebugDrawService::Unregister(InertiaDebugDrawHandle);
		InertiaDebugDrawHandle.Reset();
	}
#endif

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
	ConsumeTeleportSnap();                    // 2. Sim teleport → reset lerp buffers
	TickTimeDilation(DeltaTime);              // 3. Blink aim push/remove, stack tick, sim atomics

	// 3b. Weapon recoil + inertia (BEFORE UpdateCamera, AFTER TickTimeDilation for correct DT)
	{
		// Compute recoil DT: wall-clock when bPlayerFullSpeed, dilated otherwise
		float RecoilDT = DeltaTime;
		if (DilationStack.IsActive() && DilationStack.IsPlayerFullSpeed())
		{
			if (auto* Sub = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>())
			{
				float PublishedScale = Sub->GetSimWorker().ActiveTimeScalePublished.load(std::memory_order_relaxed);
				if (PublishedScale > 0.01f)
					RecoilDT = DeltaTime / PublishedScale;
			}
		}

		// Consume raw mouse delta captured in Look() — guaranteed recoil-free
		FVector2D MouseOnlyDelta = RecoilState.RawMouseDelta;
		RecoilState.RawMouseDelta = FVector2D::ZeroVector;

		DrainShotEventsAndApplyRecoil();
		TickKickRecovery(RecoilDT);
		TickScreenShake(RecoilDT);
		TickWeaponInertia(RecoilDT, MouseOnlyDelta);

		// Pattern reset timer
		if (RecoilState.CachedProfile && RecoilState.ShotIndex > 0)
		{
			RecoilState.PatternResetTimer += RecoilDT;
			if (RecoilState.PatternResetTimer >= RecoilState.CachedProfile->PatternResetTime)
			{
				RecoilState.ShotIndex = 0;
				RecoilState.PatternResetTimer = 0.f;
			}
		}
	}

	TickWeaponMotion(DeltaTime);              // 3c. Movement-based weapon motion (bob, tilt, landing, sprint)
	TickADS(DeltaTime);                       // 3d. ADS alpha interpolation + blocking
	TickWeaponCollision(DeltaTime);           // 3e. Weapon wall collision (raycast → ready pose blend)

	TickPostureAndResnap(DeltaTime);          // 4. Posture effects, FeetToActorOffset re-snap
	if (RopeRenderer) { RopeRenderer->Update(DeltaTime, GetWorld(), GetActorLocation()); } // 4b. Rope Verlet + Niagara
	CheckHealthChanges();                     // 5. Health change detection
	UpdateResourceUI();                       // 5b. Resource pool display
	UpdateVitalsUI();                         // 5c. Vitals display (hunger, thirst, warmth)
	TickInteractionStateMachine(DeltaTime);   // 6. Focus/Hold state machine
	UpdateCamera();                           // 7. FP position + rotation + FOV + screen shake
	WriteCameraAtomics();                     // 8. Camera pos/dir → sim thread (AFTER UpdateCamera for fresh data)
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
		// Use control rotation to avoid contamination from screen shake (AddLocalRotation)
		FVector CamDir = GetControlRotation().Vector();
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
		StateAtomics->MantleType.Read(),
		StateAtomics->ClimbActive.Read());

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

	float ADSFOVReduction = (RecoilState.CachedProfile && RecoilState.ADSAlpha > 0.f)
		? FMath::Max(0.f, FMath::Lerp(0.f, BaseFOV - RecoilState.CachedProfile->ADSFOV, RecoilState.ADSAlpha))
		: 0.f;
	FollowCamera->SetFieldOfView(BaseFOV + FatumMovement->GetCurrentFOVOffset() - ADSFOVReduction);

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

		// Screen shake: visual-only additive rotation (does NOT affect GetControlRotation)
		if (RecoilState.ShakeOffset.SizeSquared() > 0.0001f)
		{
			FollowCamera->AddLocalRotation(FRotator(RecoilState.ShakeOffset.X, RecoilState.ShakeOffset.Y, RecoilState.ShakeOffset.Z));
		}

		// Weapon transform: reset to base, then layer all offsets
		if (WeaponMeshComponent && WeaponMeshComponent->IsVisible())
		{
			// Step 0: Blend between hip pose and ADS pose
			FTransform EffectiveBase = BaseWeaponTransform;
			if (RecoilState.ADSAlpha > 0.f && RecoilState.bADSTransformValid)
			{
				EffectiveBase.BlendWith(RecoilState.ADSWeaponTransform, RecoilState.ADSAlpha);
			}
			WeaponMeshComponent->SetRelativeTransform(EffectiveBase);

			// Layer 1: Rotational inertia (aim lag)
			if (!RecoilState.InertiaOffset.IsNearlyZero(0.001f))
			{
				WeaponMeshComponent->AddLocalRotation(FRotator(RecoilState.InertiaOffset.X, RecoilState.InertiaOffset.Y, 0.f));
			}

			// Layer 2: Positional inertia (mouse-driven mesh shift)
			if (!RecoilState.InertiaPositionOffset.IsNearlyZero(0.001f))
			{
				WeaponMeshComponent->AddLocalOffset(RecoilState.InertiaPositionOffset);
			}

			// Layer 3: Movement-based motion (bob, tilt, landing, sprint, movement inertia, footsteps)
			if (!RecoilState.MotionPositionOffset.IsNearlyZero(0.001f))
			{
				WeaponMeshComponent->AddLocalOffset(RecoilState.MotionPositionOffset);
			}
			if (!RecoilState.MotionRotationOffset.IsNearlyZero(0.001f))
			{
				WeaponMeshComponent->AddLocalRotation(RecoilState.MotionRotationOffset);
			}

			// Layer 4: Wall collision — weapon retracts to ready pose near obstacles
			if (RecoilState.CollisionCurrentAlpha > 0.f)
			{
				WeaponMeshComponent->AddLocalOffset(RecoilState.CollisionPositionOffset);
				WeaponMeshComponent->AddLocalRotation(RecoilState.CollisionRotationOffset);
			}
		}
	}
}

void AFlecsCharacter::ProcessPendingWeaponEquip()
{
	if (!PendingWeaponEquip.bPending.load(std::memory_order_acquire)) return;

	PendingWeaponEquip.bPending.store(false, std::memory_order_relaxed);
	int64 WeaponId = PendingWeaponEquip.WeaponId.load(std::memory_order_acquire);

	TestWeaponEntityId = WeaponId;

	// Cache weapon profile for recoil processing and reset recoil state
	RecoilState.Reset();
	RecoilState.CachedProfile = (TestWeaponDefinition && TestWeaponDefinition->WeaponProfile)
		? TestWeaponDefinition->WeaponProfile.Get() : nullptr;

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

			// Apply weapon inertia offset: bullets go where the weapon points, not the crosshair
			if (!RecoilState.InertiaOffset.IsNearlyZero(0.001f))
			{
				FRotator AimRot = AimDir.Rotation();
				AimRot.Pitch += RecoilState.InertiaOffset.X;
				AimRot.Yaw += RecoilState.InertiaOffset.Y;
				AimDir = AimRot.Vector();
			}

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
			// Initial resource update handled by UpdateResourceUI() poll (CachedResourceRatios start at 0)
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
// DEBUG DRAW (2D screen-space weapon aim dot)
// ═══════════════════════════════════════════════════════════════════════════

#if !UE_BUILD_SHIPPING
void AFlecsCharacter::DrawInertiaDebug(UCanvas* Canvas, APlayerController* PC)
{
	if (!PC || !FollowCamera || !Canvas) return;
	if (!RecoilState.CachedProfile || RecoilState.CachedProfile->InertiaStiffness <= 0.f) return;
	FRotator AimRot = GetControlRotation();
	AimRot.Pitch += RecoilState.InertiaOffset.X;
	AimRot.Yaw += RecoilState.InertiaOffset.Y;
	FVector WorldPoint = FollowCamera->GetComponentLocation() + AimRot.Vector() * 10000.f;

	FVector2D ScreenPos;
	if (PC->ProjectWorldLocationToScreen(WorldPoint, ScreenPos))
	{
		const float DotSize = 4.f;
		FCanvasTileItem TileItem(
			FVector2D(ScreenPos.X - DotSize, ScreenPos.Y - DotSize),
			FVector2D(DotSize * 2.f, DotSize * 2.f),
			FLinearColor(0.2f, 0.47f, 1.f, 0.85f));
		TileItem.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(TileItem);
	}
}
#endif

// Input binding + handler methods: see FlecsCharacter_Input.cpp
