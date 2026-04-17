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
#include "FlecsMeleeComponents.h"
#include "FlecsItemComponents.h"
#include <bit>
#include <cstring>
#include "FlecsHealthProfile.h"
#include "FlecsSwingableComponents.h"
#include "FlecsNiagaraManager.h"
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

	InitInteractionTrace();  // _Interaction.cpp
	InitUI();                // _UI.cpp
	InitWeaponListeners();   // _ActionState.cpp

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

		// Inventory ref — always needed (weapon reload, vitals equipment scanning)
		// InventoryEntityId set later by InitInventoryContainers
		{
			FCharacterInventoryRef InvRef;
			Entity.set<FCharacterInventoryRef>(InvRef);
		}

		// Vitals: per-entity instance components (FVitalsInstance/FVitalsStatic inherited from prefab)
		if (Definition->VitalsProfile)
		{
			Entity.set<FStatModifiers>(FStatModifiers{});
			Entity.set<FEquipmentVitalsCache>(FEquipmentVitalsCache{});
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
	UpdateMeleeProceduralAnim(DeltaTime);     // 12a. TEMP (Phase 9): drive WeaponMeshComponent swing pose
	                                          //      MUST run BEFORE WriteMeleeWeaponBladeSocket so the sweep
	                                          //      reads sockets from the posed mesh, not the rest pose.
	                                          //      TODO(MIGRATE): delete when AnimMontage swing is wired.
	WriteMeleeWeaponBladeSocket(DeltaTime);   // 12. Game→sim blade socket triple buffer (Phase 3)
	UpdateMeleeBladeTrail();                  // 13. Blade trail VFX lifecycle (Phase 7)
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

	// Drain sim→game hit-stop events BEFORE ticking the stack so new entries are
	// visible to this frame's min-wins resolve (§F.3 / C1).
	DrainPendingHitStops();

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
	const bool bIsMelee = PendingWeaponEquip.bIsMelee.load(std::memory_order_acquire);
	UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] ProcessPendingWeaponEquip: WeaponId=%lld Mesh=%s Slot=%d bIsMelee=%d"),
		WeaponId,
		PendingWeaponEquip.Mesh ? *PendingWeaponEquip.Mesh->GetName() : TEXT("NULL"),
		PendingWeaponEquip.SlotIndex.load(std::memory_order_acquire),
		bIsMelee ? 1 : 0);

	if (WeaponId == 0)
	{
		// Unequip — holstered or aborted
		bActiveWeaponIsMelee = false;
		ActiveWeaponEntityId = 0;
		ActiveWeaponSlotIndex = -1;
		ActiveWeaponProfile = nullptr;
		RecoilState.Reset();
		RecoilState.CachedProfile = nullptr;
		DetachWeaponVisual();

		if (HUDWidget)
			HUDWidget->SetWeaponEntityId(0);

		ClearGameBit(ActionBit::WeaponSwitching);

		// Sync deferred sprint restore
		if (HasBit(GameActionState.load(std::memory_order_relaxed), ActionBit::Sprinting))
		{
			if (InputAtomics) InputAtomics->Sprinting.Write(true);
			if (FatumMovement) FatumMovement->RequestSprint(true);
		}

		UE_LOG(LogTemp, Log, TEXT("WEAPON: Unequipped"));
		return;
	}

	// Equip new weapon
	bActiveWeaponIsMelee = bIsMelee;
	ActiveWeaponEntityId = WeaponId;
	ActiveWeaponSlotIndex = PendingWeaponEquip.SlotIndex.load(std::memory_order_acquire);
	ActiveWeaponProfile = PendingWeaponEquip.WeaponProfile;

	RecoilState.Reset();
	RecoilState.CachedProfile = ActiveWeaponProfile;

	if (HUDWidget)
		HUDWidget->SetWeaponEntityId(WeaponId);

	// Melee equip path (Phase 3 §F.5) sends null mesh — detach any prior ranged mesh so
	// WeaponMeshComponent doesn't retain stale geometry (§0.1 fix). AttachWeaponVisual
	// early-returns on null, which would leak the previous weapon's sockets to
	// WriteMeleeWeaponBladeSocket.
	if (PendingWeaponEquip.Mesh)
	{
		AttachWeaponVisual(PendingWeaponEquip.Mesh, PendingWeaponEquip.AttachOffset);
	}
	else
	{
		DetachWeaponVisual();
	}

	ClearGameBit(ActionBit::WeaponSwitching);

	// Sync deferred sprint restore
	if (HasBit(GameActionState.load(std::memory_order_relaxed), ActionBit::Sprinting))
	{
		if (InputAtomics) InputAtomics->Sprinting.Write(true);
		if (FatumMovement) FatumMovement->RequestSprint(true);
	}

	UE_LOG(LogTemp, Log, TEXT("WEAPON: Equipped weapon %lld in slot %d"), WeaponId, ActiveWeaponSlotIndex);
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

// ═══════════════════════════════════════════════════════════════════════════
// MELEE BRIDGE (Phase 3 — §E.1 atomic, §F.3 hit-stop MPSC, §E.2 blade buffer)
// ═══════════════════════════════════════════════════════════════════════════

namespace
{
	constexpr uint64 MeleePhaseShift     = 56;
	constexpr uint64 MeleeDirectionShift = 48;
	constexpr uint64 MeleeShapedTMask    = 0x00000000FFFFFFFFull;
	constexpr uint64 MeleePhaseMask      = 0xFF00000000000000ull;
	constexpr uint64 MeleeDirectionMask  = 0x00FF000000000000ull;

	FORCEINLINE uint32 MeleeFloatToBits(float V)
	{
		// std::bit_cast is C++20 and available in UE 5.7's toolchain; fall back to memcpy
		// for maximum compiler portability (matches the pattern used elsewhere in UE).
		uint32 Bits = 0;
		static_assert(sizeof(Bits) == sizeof(V), "float must be 32 bits");
		std::memcpy(&Bits, &V, sizeof(Bits));
		return Bits;
	}

	FORCEINLINE float MeleeBitsToFloat(uint32 Bits)
	{
		float V = 0.f;
		std::memcpy(&V, &Bits, sizeof(V));
		return V;
	}
}

void AFlecsCharacter::PublishMeleeAttackState(EMeleeAttackPhase Phase, EMeleeSwingDirection Direction, float ShapedT)
{
	const uint64 PhaseBits     = static_cast<uint64>(static_cast<uint8>(Phase))     << MeleePhaseShift;
	const uint64 DirectionBits = static_cast<uint64>(static_cast<uint8>(Direction)) << MeleeDirectionShift;
	const uint64 ShapedTBits   = static_cast<uint64>(MeleeFloatToBits(ShapedT))     & MeleeShapedTMask;

	MeleeAttackStatePacked.store(PhaseBits | DirectionBits | ShapedTBits, std::memory_order_release);
}

void AFlecsCharacter::ReadMeleeAttackState(EMeleeAttackPhase& OutPhase, EMeleeSwingDirection& OutDirection, float& OutShapedT) const
{
	const uint64 Packed = MeleeAttackStatePacked.load(std::memory_order_acquire);

	OutPhase     = static_cast<EMeleeAttackPhase>(    static_cast<uint8>((Packed & MeleePhaseMask)     >> MeleePhaseShift));
	OutDirection = static_cast<EMeleeSwingDirection>( static_cast<uint8>((Packed & MeleeDirectionMask) >> MeleeDirectionShift));
	OutShapedT   = MeleeBitsToFloat(static_cast<uint32>(Packed & MeleeShapedTMask));
}

void AFlecsCharacter::EnqueueHitStop(const FPendingHitStopEvent& Ev)
{
	// MPSC: safe from any thread. Game thread drains in TickTimeDilation.
	PendingHitStopQueue.Enqueue(Ev);
}

void AFlecsCharacter::DrainPendingHitStops()
{
	FPendingHitStopEvent Ev;
	while (PendingHitStopQueue.Dequeue(Ev))
	{
		FDilationEntry Entry;
		Entry.Tag              = Ev.Tag;
		Entry.DesiredScale     = Ev.Scale;
		Entry.Duration         = Ev.Duration;
		Entry.bPlayerFullSpeed = true;
		Entry.EntrySpeed       = Ev.EntrySpeed;
		Entry.ExitSpeed        = Ev.ExitSpeed;
		DilationStack.Push(Entry);
	}
}

void AFlecsCharacter::WriteMeleeWeaponBladeSocket(float /*DeltaTime*/)
{
	// Only sample while the equipped weapon is a melee weapon in an active attack phase.
	// During Idle/Charging/Recovery the sweep system does not read the buffer, so writing
	// is wasted work and just thrashes the triple buffer's write slot.
	if (ActiveWeaponEntityId == 0 || !WeaponMeshComponent)
	{
		return;
	}

	// Defensive: a melee equip with null mesh + prior ranged detach means the skeletal
	// mesh asset is cleared. Socket lookups on a null asset return garbage (§0.1 fix).
	if (!WeaponMeshComponent->GetSkeletalMeshAsset())
	{
		return;
	}

	EMeleeAttackPhase     Phase     = EMeleeAttackPhase::Idle;
	EMeleeSwingDirection  Direction = EMeleeSwingDirection::Horizontal;
	float                 ShapedT   = 0.f;
	ReadMeleeAttackState(Phase, Direction, ShapedT);

	if (Phase != EMeleeAttackPhase::Windup && Phase != EMeleeAttackPhase::Release)
	{
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[MELEE-DBG] WriteBladeSocket: Phase=%d (Release=3) — proceeding to socket read"),
		(int32)Phase);

	UFlecsArtillerySubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>() : nullptr;
	if (!Sub)
	{
		return;
	}

	flecs::world* World = Sub->GetFlecsWorld();
	if (!World)
	{
		return;
	}

	flecs::entity WeaponEntity = World->entity(static_cast<flecs::entity_t>(ActiveWeaponEntityId));
	if (!WeaponEntity.is_valid() || !WeaponEntity.is_alive())
	{
		return;
	}

	const FMeleeWeaponStatic* MStatic = WeaponEntity.try_get<FMeleeWeaponStatic>();
	FMeleeWeaponInstance*     MInst   = WeaponEntity.try_get_mut<FMeleeWeaponInstance>();
	if (!MStatic || !MInst || !MInst->BladeBuffer)
	{
		return;
	}

	const FVector Start = WeaponMeshComponent->GetSocketLocation(MStatic->BladeStartSocket);
	const FVector Tip   = WeaponMeshComponent->GetSocketLocation(MStatic->BladeTipSocket);

	FMeleeWeaponInstance::FBladeSocketData Sample;
	Sample.Start      = Start;
	Sample.Tip        = Tip;
	Sample.FrameStamp = static_cast<uint64>(GFrameCounter);

	// CRITICAL: Write() alone only updates the current write slot — it does NOT
	// swap buffers or mark the triple buffer dirty. Without SwapWriteBuffers the
	// reader's SwapReadBuffers() sees IsDirty()==false and never swaps, leaving
	// it stuck on the initial default slot (frameStamp=0 forever). WriteAndSwap()
	// does both atomically per UE TTripleBuffer contract (Containers/TripleBuffer.h L231).
	MInst->BladeBuffer->WriteAndSwap(Sample);

	UE_LOG(LogTemp, Warning,
		TEXT("[MELEE-DBG] WriteBladeSocket WROTE: frameStamp=%llu start=(%s) tip=(%s) BufferPtr=%p"),
		(uint64)Sample.FrameStamp, *Start.ToString(), *Tip.ToString(), MInst->BladeBuffer);
}

void AFlecsCharacter::UpdateMeleeBladeTrail()
{
	// Blade trail lifecycle: start trail when entering Release, stop when leaving.
	// All UObject access (WeaponMeshComponent, NiagaraManager) is game-thread-safe.
	EMeleeAttackPhase     Phase     = EMeleeAttackPhase::Idle;
	EMeleeSwingDirection  Direction = EMeleeSwingDirection::Horizontal;
	float                 ShapedT   = 0.f;
	ReadMeleeAttackState(Phase, Direction, ShapedT);

	if (Phase == EMeleeAttackPhase::Release && !bMeleeTrailActive)
	{
		// Start trail — need weapon static for TrailEffect + socket names.
		if (ActiveWeaponEntityId == 0 || !WeaponMeshComponent) return;

		UFlecsArtillerySubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>() : nullptr;
		if (!Sub) return;

		flecs::world* World = Sub->GetFlecsWorld();
		if (!World) return;

		flecs::entity WeaponEntity = World->entity(static_cast<flecs::entity_t>(ActiveWeaponEntityId));
		if (!WeaponEntity.is_valid() || !WeaponEntity.is_alive()) return;

		const FMeleeWeaponStatic* MStatic = WeaponEntity.try_get<FMeleeWeaponStatic>();
		if (!MStatic || !MStatic->TrailEffect) return;

		UFlecsNiagaraManager* NiagaraMgr = UFlecsNiagaraManager::Get(GetWorld());
		if (!NiagaraMgr) return;

		NiagaraMgr->EnqueueBladeTrail(
			static_cast<uint64>(ActiveWeaponEntityId),
			MStatic->TrailEffect,
			WeaponMeshComponent,
			MStatic->BladeStartSocket,
			MStatic->BladeTipSocket);
		bMeleeTrailActive = true;
	}
	else if (Phase != EMeleeAttackPhase::Release && bMeleeTrailActive)
	{
		// Stop trail.
		UFlecsNiagaraManager* NiagaraMgr = UFlecsNiagaraManager::Get(GetWorld());
		if (NiagaraMgr)
		{
			NiagaraMgr->DequeueBladeTrailDetach(static_cast<uint64>(ActiveWeaponEntityId));
		}
		bMeleeTrailActive = false;
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// TEMP (Phase 9): PROCEDURAL MELEE WEAPON-MESH ANIMATION
// ═══════════════════════════════════════════════════════════════════════════
//
// TODO(MIGRATE): This entire block — UpdateMeleeProceduralAnim + the three
// anon-namespace pose-table helpers below — is a placeholder for a real
// UAnimMontage/UAnimBlueprint swing animation. When that pipeline lands:
//   1. Delete this section (from namespace TempMeleeAnim through the
//      closing brace of UpdateMeleeProceduralAnim).
//   2. Remove the call from AFlecsCharacter::Tick (grep for "Phase 9").
//   3. Remove the public UPROPERTY / private state declarations in the header
//      (grep for "Phase 9").
//   4. Remove the two bMeleeRestTransformCached resets in FlecsCharacter_Combat.cpp
//      AttachWeaponVisual / DetachWeaponVisual.
// No sim-thread code (MeleeSweepSystem, MeleeChargeSystem, etc.) reads anything
// defined here — the procedural path consumes the same MeleeAttackStatePacked
// atomic the sim publishes. Deletion is a pure subtraction.

namespace TempMeleeAnim
{
	// Per-direction pose targets. TODO(MIGRATE): These tables become AnimSequences
	// authored in UE. Units: degrees (applied as FRotator(Pitch,Yaw,Roll)).
	struct FDirectionPose
	{
		float WindupYawDeg   = 0.f;
		float WindupPitchDeg = 0.f;
		float WindupRollDeg  = 0.f;

		/** Release arc is obtained by adding (SweepYawSign, SweepPitchSign) *
		 *  MeleeProceduralReleaseSweepDegrees to the windup rotation — designer
		 *  tunes magnitude at runtime without touching code. */
		float SweepYawSign   = 0.f;
		float SweepPitchSign = 0.f;
		float SweepRollSign  = 0.f;

		/** Camera-space LOCAL offset at the end of windup (ratio of BackOffsetCm).
		 *  X = forward(+) / back(-), Y = right(+) / left(-), Z = up(+) / down(-).
		 *  Gives each direction a visually distinct starting pose — without lateral
		 *  motion the eye sees only "sword rotates around hilt" and can't tell swings
		 *  apart. Scaled by MeleeProceduralWindupBackOffset so designer can tune
		 *  intensity. */
		FVector WindupOffsetMul = FVector(-1.f, 0.f, 0.f);

		/** Camera-space LOCAL offset at the end of release (same scaling as windup).
		 *  Drives the observable arc: weapon travels from WindupOffsetMul → ReleaseOffsetMul
		 *  during Release phase → back to zero during Recovery. */
		FVector ReleaseOffsetMul = FVector(1.f, 0.f, 0.f);
	};

	static FDirectionPose GetPoseForDirection(EMeleeSwingDirection Dir)
	{
		FDirectionPose P;
		// Offset convention (camera-local; multiplied by MeleeProceduralWindupBackOffset):
		//   X: forward(+) / back(-)      — depth along camera look direction
		//   Y: right(+)   / left(-)      — lateral
		//   Z: up(+)      / down(-)      — vertical
		switch (Dir)
		{
			case EMeleeSwingDirection::Horizontal:
				// Right slash (cock RIGHT, sweep to LEFT across the screen).
				P.WindupYawDeg   = -90.f;
				P.WindupPitchDeg =  20.f;
				P.WindupRollDeg  =  30.f;
				P.SweepYawSign   =  1.5f;     // yaw sweeps strongly left→right
				P.SweepPitchSign = -0.05f;
				P.SweepRollSign  = -0.3f;
				// Windup: weapon goes BACK + RIGHT + slightly up (over right shoulder).
				P.WindupOffsetMul  = FVector(-0.6f,  1.5f,  0.4f);
				// Release: weapon slashes FAR-LEFT and forward.
				P.ReleaseOffsetMul = FVector( 0.8f, -1.8f, -0.3f);
				break;

			case EMeleeSwingDirection::Vertical:
				// Overhead chop: cock UP, sweep DOWN.
				P.WindupYawDeg   = 0.f;
				P.WindupPitchDeg = 80.f;
				P.WindupRollDeg  = 0.f;
				P.SweepYawSign   = 0.f;
				P.SweepPitchSign = -1.5f;
				P.SweepRollSign  = 0.f;
				// Windup: straight UP above the head.
				P.WindupOffsetMul  = FVector(-0.3f,  0.0f,  1.8f);
				// Release: DOWN and forward — hits the ground in front.
				P.ReleaseOffsetMul = FVector( 0.9f,  0.0f, -1.2f);
				break;

			case EMeleeSwingDirection::DiagonalTL:
				// Upper-LEFT → lower-RIGHT (slash from player's left-shoulder direction).
				P.WindupYawDeg   = -45.f;
				P.WindupPitchDeg =  50.f;
				P.WindupRollDeg  =  20.f;
				P.SweepYawSign   =  1.2f;
				P.SweepPitchSign = -1.0f;
				P.SweepRollSign  = -0.3f;
				P.WindupOffsetMul  = FVector(-0.4f, -1.4f,  1.3f);
				P.ReleaseOffsetMul = FVector( 0.8f,  1.6f, -1.0f);
				break;

			case EMeleeSwingDirection::DiagonalTR:
				// Upper-RIGHT → lower-LEFT (mirror of TL).
				P.WindupYawDeg   =  45.f;
				P.WindupPitchDeg =  50.f;
				P.WindupRollDeg  = -20.f;
				P.SweepYawSign   = -1.2f;
				P.SweepPitchSign = -1.0f;
				P.SweepRollSign  =  0.3f;
				P.WindupOffsetMul  = FVector(-0.4f,  1.4f,  1.3f);
				P.ReleaseOffsetMul = FVector( 0.8f, -1.6f, -1.0f);
				break;

			case EMeleeSwingDirection::Thrust:
				// Straight jab: pull straight BACK, then THRUST FORWARD hard.
				P.WindupYawDeg   = 0.f;
				P.WindupPitchDeg = -10.f;
				P.WindupRollDeg  = 0.f;
				P.SweepYawSign   = 0.f;
				P.SweepPitchSign = 0.f;
				P.SweepRollSign  = 0.f;
				P.WindupOffsetMul  = FVector(-2.0f, 0.0f, 0.0f);
				P.ReleaseOffsetMul = FVector( 2.5f, 0.0f, 0.0f);
				break;

			default:
				break;
		}
		return P;
	}

	/** Apply a FDirectionPose at normalized pose time [0,1]: 0 = rest, 1 = release end.
	 *  Caller chooses WindupBlend and ReleaseBlend to blend between phases. */
	static FTransform BuildSwingOffset(const FDirectionPose& Pose,
	                                   float BackOffsetCm,
	                                   float SweepDegrees,
	                                   float WindupBlend,
	                                   float ReleaseBlend)
	{
		// Rotation: interpolated between rest(0) → windup → release-end.
		//   Windup pose rotation = (Pitch,Yaw,Roll) × WindupBlend
		//   Release adds sweep on top: Windup + Sweep × ReleaseBlend
		const float Yaw   = Pose.WindupYawDeg   * WindupBlend + Pose.SweepYawSign   * SweepDegrees * ReleaseBlend;
		const float Pitch = Pose.WindupPitchDeg * WindupBlend + Pose.SweepPitchSign * SweepDegrees * ReleaseBlend;
		const float Roll  = Pose.WindupRollDeg  * WindupBlend + Pose.SweepRollSign  * SweepDegrees * ReleaseBlend;

		// Translation: camera-local blend from rest (0) → WindupOffsetMul during windup
		// → ReleaseOffsetMul during release. Both offsets come from the direction-specific
		// pose table. Scaled by BackOffsetCm so designers can tune "amplitude" globally.
		//
		// Blend math: at WindupBlend=1, ReleaseBlend=0 → WindupOffset.
		//             at WindupBlend=1, ReleaseBlend=1 → ReleaseOffset (end of strike).
		//             (windup is held at 1 throughout Release; Release ramps 0→1.)
		// Mixed: we can't simply add — want a LERP from Windup to Release as ReleaseBlend ramps.
		const FVector WindupPos  = Pose.WindupOffsetMul  * BackOffsetCm;
		const FVector ReleasePos = Pose.ReleaseOffsetMul * BackOffsetCm;
		const FVector LerpedArc  = FMath::Lerp(WindupPos, ReleasePos, FMath::Clamp(ReleaseBlend, 0.f, 1.f));
		// Recovery case: WindupBlend fades to 0 while ReleaseBlend is also fading to 0 →
		// result naturally settles to zero (rest).
		const FVector Translation = LerpedArc * FMath::Clamp(WindupBlend, 0.f, 1.f);

		FTransform Offset;
		Offset.SetRotation(FQuat(FRotator(Pitch, Yaw, Roll)));
		Offset.SetTranslation(Translation);
		return Offset;
	}
}

// TEMP (Phase 9): drive WeaponMeshComponent relative transform from
// MeleeAttackStatePacked so the player sees the swing AND the blade sockets
// trace meaningful world positions for MeleeSweepSystem.
// TODO(MIGRATE): replace with UAnimMontage on WeaponMeshComponent's AnimInstance.
void AFlecsCharacter::UpdateMeleeProceduralAnim(float DeltaTime)
{
	// Gate 1 — master enable. Designers can toggle in editor for AnimMontage A/B.
	if (!bMeleeProceduralAnimEnabled)
	{
		return;
	}

	// Gate 2 — need a weapon mesh with actual geometry. Same early-out pattern as
	// WriteMeleeWeaponBladeSocket so a null skel asset doesn't read garbage sockets.
	if (!WeaponMeshComponent || !WeaponMeshComponent->GetSkeletalMeshAsset())
	{
		return;
	}

	// Gate 3 — must be a melee-equipped weapon (FMeleeWeaponInstance present on the
	// Flecs weapon entity). Ranged weapons skip this path entirely.
	if (ActiveWeaponEntityId == 0)
	{
		return;
	}

	UFlecsArtillerySubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>() : nullptr;
	if (!Sub)
	{
		return;
	}
	flecs::world* World = Sub->GetFlecsWorld();
	if (!World)
	{
		return;
	}

	flecs::entity WeaponEntity = World->entity(static_cast<flecs::entity_t>(ActiveWeaponEntityId));
	if (!WeaponEntity.is_valid() || !WeaponEntity.is_alive())
	{
		return;
	}

	const FMeleeWeaponStatic*   MStatic = WeaponEntity.try_get<FMeleeWeaponStatic>();
	const FMeleeWeaponInstance* MInst   = WeaponEntity.try_get<FMeleeWeaponInstance>();
	if (!MStatic || !MInst)
	{
		// Not a melee weapon — bail. UpdateCamera() already set the mesh transform
		// via the recoil/inertia stack; do not touch it here.
		return;
	}

	// Pull published melee state (written by sim thread on transitions).
	EMeleeAttackPhase     Phase     = EMeleeAttackPhase::Idle;
	EMeleeSwingDirection  Direction = EMeleeSwingDirection::Horizontal;
	float                 ShapedT   = 0.f; // unused today — procedural poses are purely phase-driven.
	ReadMeleeAttackState(Phase, Direction, ShapedT);

	// Cache the rest transform the FIRST time we observe Idle. This is whatever
	// UpdateCamera() just wrote this tick (BaseWeaponTransform + recoil/inertia
	// layers = the live hip pose). All procedural swing math composes against
	// this cached rest — which means we OVERRIDE the recoil/inertia stack while
	// swinging (intentional: AnimMontage will do the same when it lands, so the
	// procedural path mirrors the future migration target's visual contract).
	//
	// We re-cache on weapon equip (see AttachWeaponVisual / DetachWeaponVisual).
	if (!bMeleeRestTransformCached)
	{
		if (Phase != EMeleeAttackPhase::Idle)
		{
			// First tick after equip landed mid-swing (unlikely but possible on
			// hot-swap). Skip — next Idle will cache cleanly.
			return;
		}
		MeleeRestTransform        = WeaponMeshComponent->GetRelativeTransform();
		bMeleeRestTransformCached = true;
	}

	// Track time-in-phase on game thread. Reset on observed phase transition.
	// Drift vs. sim PhaseTimer is cosmetic only (phase durations match MStatic),
	// and any drift self-corrects on the next transition.
	if (Phase != MeleePrevPhase)
	{
		MeleePhaseElapsed = 0.f;
		MeleePrevPhase    = Phase;
	}
	else
	{
		MeleePhaseElapsed += DeltaTime;
	}

	// Compute (WindupBlend, ReleaseBlend) ∈ [0,1]² per current phase.
	//   Idle      → (0, 0)                — rest
	//   Charging  → (ShapedT, 0)          — Mordhau-style: cock-back scales with how long
	//                                       LMB has been held. ShapedT is published by
	//                                       MeleeChargeSystem every sim tick (accumulator /
	//                                       MaxChargeTime, curve-shaped).
	//   Windup    → (easeInOut(t), 0)     — LEGACY: retained for future AnimMontage pre-
	//                                       strike timing; charge path skips this phase.
	//   Release   → (1, t)                — sweep windup → end
	//   Recovery  → (easeOut(1-t), 1-t)   — settle back to rest
	// phase-internal t = clamp(elapsed / duration, 0, 1). For Windup/Release/Recovery
	// the durations live on MStatic (WindupTime / ReleaseTime / RecoveryTime).
	float WindupBlend  = 0.f;
	float ReleaseBlend = 0.f;

	switch (Phase)
	{
		case EMeleeAttackPhase::Idle:
			// Rest pose. Snap cleanly (no blend) — handles interrupted swings.
			WindupBlend  = 0.f;
			ReleaseBlend = 0.f;
			break;

		case EMeleeAttackPhase::Charging:
			// Cock-back scales with charge progress. ShapedT is published live by
			// MeleeChargeSystem (charge accumulator / MaxChargeTime, curve-shaped).
			WindupBlend  = FMath::Clamp(ShapedT, 0.f, 1.f);
			ReleaseBlend = 0.f;
			break;

		case EMeleeAttackPhase::Windup:
		{
			const float Duration = FMath::Max(MStatic->WindupTime, KINDA_SMALL_NUMBER);
			const float t        = FMath::Clamp(MeleePhaseElapsed / Duration, 0.f, 1.f);
			WindupBlend  = FMath::InterpEaseInOut(0.f, 1.f, t, 2.f);
			ReleaseBlend = 0.f;
			break;
		}

		case EMeleeAttackPhase::Release:
		{
			const float Duration = FMath::Max(MStatic->ReleaseTime, KINDA_SMALL_NUMBER);
			const float t        = FMath::Clamp(MeleePhaseElapsed / Duration, 0.f, 1.f);
			// Weapon is fully cocked at release start, sweeps through arc.
			WindupBlend  = 1.f;
			// Slight ease-in so the swing accelerates (tip-speed thump feel).
			ReleaseBlend = FMath::InterpEaseIn(0.f, 1.f, t, 1.6f);
			break;
		}

		case EMeleeAttackPhase::Recovery:
		{
			const float Duration = FMath::Max(MStatic->RecoveryTime, KINDA_SMALL_NUMBER);
			const float t        = FMath::Clamp(MeleePhaseElapsed / Duration, 0.f, 1.f);
			// Start at end-of-release pose, settle back to rest. Soft ease-out.
			const float Settle   = FMath::InterpEaseOut(1.f, 0.f, t, 2.f);
			WindupBlend  = Settle;
			ReleaseBlend = Settle;
			break;
		}

		default:
			break;
	}

	// Build the pose offset for this (Direction, blends, designer tunables).
	const TempMeleeAnim::FDirectionPose Pose = TempMeleeAnim::GetPoseForDirection(Direction);
	const FTransform SwingOffset = TempMeleeAnim::BuildSwingOffset(
		Pose,
		MeleeProceduralWindupBackOffset,
		MeleeProceduralReleaseSweepDegrees,
		WindupBlend,
		ReleaseBlend);

	// Apply: Final = SwingOffset (parent-space) * MeleeRestTransform.
	//
	// We OVERRIDE the recoil/inertia stack UpdateCamera() wrote this tick — the
	// swing animation is authoritative while in motion. SwingOffset is authored
	// in camera-space (parent of WeaponMeshComponent), so left-multiplication
	// treats it as a parent transform applied to the cached rest pose.
	//
	// During Idle/Charging both blends are 0; we LEAVE the mesh transform alone
	// (UpdateCamera's recoil/inertia/motion stays visible). The very first
	// post-Recovery tick that lands back in Idle still has Recovery's transform
	// painted by UpdateCamera; UpdateCamera writes BaseWeaponTransform fresh
	// every tick, so the recovery pose vanishes naturally on the next frame.
	//
	// Interruption safety: if a swing is force-cancelled (e.g. holster mid-
	// Release pushes Phase straight to Idle), this branch skips writing →
	// UpdateCamera's already-applied BaseWeaponTransform stands → mesh snaps
	// cleanly to rest. No partial-pose lingering.
	//
	// TODO(MIGRATE): AnimMontage will replace this — UpdateCamera will keep
	// driving the relative transform between swings, AnimMontage takes over
	// during swings via the AnimInstance (which is layered after SetRelativeTransform).
	if (Phase == EMeleeAttackPhase::Charging
	 || Phase == EMeleeAttackPhase::Windup
	 || Phase == EMeleeAttackPhase::Release
	 || Phase == EMeleeAttackPhase::Recovery)
	{
		WeaponMeshComponent->SetRelativeTransform(SwingOffset * MeleeRestTransform);
	}
}
