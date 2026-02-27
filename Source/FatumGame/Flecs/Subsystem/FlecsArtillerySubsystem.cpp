// FlecsArtillerySubsystem - Core Lifecycle, Simulation Thread, and Tick
//
// This file contains:
// - Subsystem lifecycle (Initialize, OnWorldBeginPlay, Deinitialize)
// - Simulation thread management (Start/Stop)
// - DrainCommandQueue + ProgressWorld (called by FSimulationWorker)
// - EnqueueCommand (thread-safe command queue)
//
// See also:
// - FlecsArtillerySubsystem_Systems.cpp   - Flecs systems setup
// - FlecsArtillerySubsystem_Collision.cpp - Collision handling
// - FlecsArtillerySubsystem_Binding.cpp   - Bidirectional binding API
// - FlecsArtillerySubsystem_Items.cpp     - Item prefab registry

#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FlecsGameTags.h"
#include "FlecsRenderManager.h"
#include "FlecsNiagaraManager.h"
#include "FDebrisPool.h"
#include "FlecsCharacter.h"
#include "FlecsMovementStatic.h"
#include "FWorldSimOwner.h"
#include "HAL/PlatformTime.h"

// ═══════════════════════════════════════════════════════════════
// SIMULATION THREAD API (called by FSimulationWorker)
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::DrainCommandQueue()
{
	TFunction<void()> Command;
	while (CommandQueue.Dequeue(Command))
	{
		Command();
	}
}

void UFlecsArtillerySubsystem::ProgressWorld(float DeltaTime)
{
	if (FlecsWorld)
	{
		FlecsWorld->progress(DeltaTime);
	}
}

void UFlecsArtillerySubsystem::EnsureBarrageAccess()
{
	// thread_local guard: GrantClientFeed has an FScopeLock, so only call once per thread.
	thread_local bool bRegistered = false;
	if (!bRegistered && CachedBarrageDispatch)
	{
		CachedBarrageDispatch->GrantClientFeed();
		bRegistered = true;
	}
}

void UFlecsArtillerySubsystem::ApplyLateSyncBuffers()
{
	if (LateSyncBridge && FlecsWorld)
	{
		LateSyncBridge->ApplyAll(FlecsWorld.Get());
	}
}

// ═══════════════════════════════════════════════════════════════
// CHARACTER PHYSICS BRIDGE
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::RegisterCharacterBridge(AFlecsCharacter* Character)
{
	check(Character);
	check(Character->PosAtomics.IsValid());
	check(Character->CachedBarrageBody.IsValid());
	check(Character->InputAtomics.IsValid());

	FCharacterPhysBridge Bridge;
	Bridge.CachedBody = Character->CachedBarrageBody;
	Bridge.PosAtomics = Character->PosAtomics;
	Bridge.InputAtomics = Character->InputAtomics;
	Bridge.CharacterKey = Character->CharacterKey;

	// Resolve Flecs entity for this character (bidirectional binding already set)
	Bridge.Entity = GetEntityForBarrageKey(Character->CharacterKey);

	// Cache FBCharacterBase pointer for direct sim-thread access (no per-tick lookup)
	if (CachedBarrageDispatch && CachedBarrageDispatch->JoltGameSim
		&& CachedBarrageDispatch->JoltGameSim->CharacterToJoltMapping)
	{
		TSharedPtr<FBCharacterBase>* CharPtr =
			CachedBarrageDispatch->JoltGameSim->CharacterToJoltMapping->Find(
				Character->CachedBarrageBody->KeyIntoBarrage);
		if (CharPtr && *CharPtr)
		{
			Bridge.CachedFBChar = *CharPtr;
		}
	}

	CharacterBridges.Add(MoveTemp(Bridge));
}

void UFlecsArtillerySubsystem::UnregisterCharacterBridge(FSkeletonKey CharacterKey)
{
	CharacterBridges.RemoveAll([CharacterKey](const FCharacterPhysBridge& B) { return B.CharacterKey == CharacterKey; });
}

void UFlecsArtillerySubsystem::SyncCharacterPositions()
{
	for (FCharacterPhysBridge& Bridge : CharacterBridges)
	{
		if (!Bridge.PosAtomics.IsValid()) continue;
		if (!FBarragePrimitive::IsNotNull(Bridge.CachedBody)) continue;

		FVector3f Pos = FBarragePrimitive::GetPosition(Bridge.CachedBody);

		// NaN guard: GetPosition returns NaN on lookup failure. Don't write NaN to atomics —
		// game thread would SetActorLocation(NaN) and corrupt LastBarragePosition.
		// Root cause (zero-velocity Normalized) is fixed — this is a safety net.
		if (FMath::IsNaN(Pos.X) || FMath::IsNaN(Pos.Y) || FMath::IsNaN(Pos.Z))
		{
			ensureMsgf(false, TEXT("SyncCharacterPositions: NaN from GetPosition — body mapping corrupt?"));
			continue;
		}

		Bridge.PosAtomics->PosX.store(Pos.X, std::memory_order_relaxed);
		Bridge.PosAtomics->PosY.store(Pos.Y, std::memory_order_relaxed);
		Bridge.PosAtomics->PosZ.store(Pos.Z, std::memory_order_relaxed);

		// Ground state from CharacterVirtual
		uint8 GS = static_cast<uint8>(FBarragePrimitive::GetCharacterGroundState(Bridge.CachedBody));
		Bridge.PosAtomics->GroundState.store(GS, std::memory_order_relaxed);

		// Velocity in UE cm/s
		FVector3f Vel = FBarragePrimitive::GetVelocity(Bridge.CachedBody);
		Bridge.PosAtomics->VelX.store(Vel.X, std::memory_order_relaxed);
		Bridge.PosAtomics->VelY.store(Vel.Y, std::memory_order_relaxed);
		Bridge.PosAtomics->VelZ.store(Vel.Z, std::memory_order_relaxed);

		// bValid is the "version guard" — store LAST so game thread sees consistent data
		Bridge.PosAtomics->bValid.store(true, std::memory_order_release);
	}
}

// ═══════════════════════════════════════════════════════════════
// PREPARE CHARACTER STEP (sim thread, before StackUp)
// Reads all inputs + Flecs components + Jolt state,
// computes acceleration-smoothed velocity, writes to FBCharacter.mLocomotionUpdate.
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::PrepareCharacterStep(float DeltaTime)
{
	for (FCharacterPhysBridge& Bridge : CharacterBridges)
	{
		if (!Bridge.CachedFBChar || !Bridge.InputAtomics) continue;
		FBCharacterBase* FBChar = Bridge.CachedFBChar.Get();

		// 1. Read input direction from atomics (latest-wins, lock-free)
		float DirX = Bridge.InputAtomics->DirX.load(std::memory_order_relaxed);
		float DirZ = Bridge.InputAtomics->DirZ.load(std::memory_order_relaxed);

		// 2. Read movement params from Flecs
		const FMovementStatic* MS = Bridge.Entity.is_valid() ? Bridge.Entity.try_get<FMovementStatic>() : nullptr;
		const FCharacterMoveState* State = Bridge.Entity.is_valid() ? Bridge.Entity.try_get<FCharacterMoveState>() : nullptr;
		if (!MS)
		{
			FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
			continue;
		}

		// 3. Slide deceleration (sim-thread owned)
		FSlideInstance* Slide = Bridge.Entity.try_get_mut<FSlideInstance>();
		bool bSliding = false;
		if (Slide)
		{
			Slide->CurrentSpeed -= MS->SlideDeceleration * DeltaTime;
			Slide->CurrentSpeed = FMath::Max(Slide->CurrentSpeed, 0.f);
			Slide->Timer -= DeltaTime;
			bSliding = true;

			// Exit conditions (sim-thread decides)
			bool bOnGroundForSlide = (FBChar->mCharacter->GetGroundState()
				== JPH::CharacterVirtual::EGroundState::OnGround);
			if (Slide->Timer <= 0.f || Slide->CurrentSpeed < MS->SlideMinExitSpeed || !bOnGroundForSlide)
			{
				Bridge.Entity.remove<FSlideInstance>();
				bSliding = false;
			}
		}
		if (Bridge.PosAtomics)
		{
			Bridge.PosAtomics->bSlideActive.store(bSliding, std::memory_order_relaxed);
		}

		// 4. Compute target speed from posture/sprint/slide
		float TargetSpeedCm;
		float AccelCm;
		if (bSliding)
		{
			TargetSpeedCm = Slide->CurrentSpeed;
			AccelCm = MS->SlideMinAcceleration;  // minimal steering
		}
		else if (State && State->bSprinting && State->Posture == 0)
		{
			TargetSpeedCm = MS->SprintSpeed;
			AccelCm = MS->SprintAcceleration;
		}
		else
		{
			TargetSpeedCm = MS->WalkSpeed;
			AccelCm = MS->GroundAcceleration;
			if (State)
			{
				switch (State->Posture)
				{
				case 1: TargetSpeedCm = MS->CrouchSpeed; break;
				case 2: TargetSpeedCm = MS->ProneSpeed; break;
				}
			}
		}

		// 5. Ground state from Jolt CharacterVirtual
		bool bOnGround = (FBChar->mCharacter->GetGroundState()
			== JPH::CharacterVirtual::EGroundState::OnGround);

		// 6. Deceleration + air accel (cm/s^2)
		float DecelCm = MS->GroundDeceleration;
		float AirAccelCm = MS->AirAcceleration;

		// 7. Build target horizontal velocity (UE cm/s → Jolt m/s)
		float DirLen = FMath::Sqrt(DirX * DirX + DirZ * DirZ);
		JPH::Vec3 TargetH = JPH::Vec3::sZero();
		if (DirLen > 0.01f)
		{
			float InvDirLen = 1.f / DirLen;
			float SpeedJolt = TargetSpeedCm / 100.f;  // cm→m
			TargetH = JPH::Vec3(DirX * InvDirLen * SpeedJolt, 0, DirZ * InvDirLen * SpeedJolt);
		}

		// 8. Read current horizontal from CharacterVirtual
		JPH::Vec3 CurVelo = FBChar->mCharacter->GetLinearVelocity();
		JPH::Vec3 CurH(CurVelo.GetX(), 0, CurVelo.GetZ());

		// 9. Pick accel rate (m/s^2)
		float AccelRate;
		if (bOnGround)
			AccelRate = TargetH.IsNearZero() ? (DecelCm / 100.f) : (AccelCm / 100.f);
		else
			AccelRate = AirAccelCm / 100.f;

		// 10. MoveTowards: smooth current → target
		JPH::Vec3 Diff = TargetH - CurH;
		float DiffLen = Diff.Length();
		float Step = AccelRate * DeltaTime;
		JPH::Vec3 SmoothedH;
		if (DiffLen <= Step || DiffLen < 1.0e-6f)
			SmoothedH = TargetH;
		else
			SmoothedH = CurH + (Diff / DiffLen) * Step;

		// 11. Write pre-smoothed velocity to FBCharacter (consumed by StepCharacter)
		FBChar->mLocomotionUpdate = SmoothedH;
	}
}

// ═══════════════════════════════════════════════════════════════
// LOCAL PLAYER REGISTRATION
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::RegisterLocalPlayer(AActor* Player, FSkeletonKey Key)
{
	CachedLocalPlayerActor = Player;
	CachedLocalPlayerKey = Key;
}

void UFlecsArtillerySubsystem::UnregisterLocalPlayer()
{
	CachedLocalPlayerActor = nullptr;
	CachedLocalPlayerKey = FSkeletonKey();
}

// ═══════════════════════════════════════════════════════════════
// SUBSYSTEM LIFECYCLE
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UBarrageDispatch>();
	Super::Initialize(Collection);
}

void UFlecsArtillerySubsystem::PostInitialize()
{
	Super::PostInitialize();
}

void UFlecsArtillerySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	CachedBarrageDispatch = GetWorld()->GetSubsystem<UBarrageDispatch>();
	checkf(CachedBarrageDispatch, TEXT("FlecsArtillerySubsystem: UBarrageDispatch must be available at BeginPlay"));

	// Register game thread with Barrage for physics reads (needed by UFlecsRenderManager::UpdateTransforms)
	CachedBarrageDispatch->GrantClientFeed();

	// Create late-sync bridge for lock-free game→sim data
	LateSyncBridge = MakeUnique<FLateSyncBridge>();

	// Create Flecs world
	FlecsWorld = MakeUnique<flecs::world>();
	checkf(FlecsWorld.IsValid(), TEXT("FlecsArtillerySubsystem: Failed to create Flecs world"));

	int32 NumWorkers = FMath::Max(2, FPlatformMisc::NumberOfCoresIncludingHyperthreads() - 2);
	FlecsWorld->set_threads(NumWorkers);
	UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: %d Flecs worker threads enabled"), NumWorkers);

	// Create debris pool for fragment body reuse
	DebrisPool = new FDebrisPool();
	DebrisPool->Initialize(CachedBarrageDispatch);

	SetupFlecsSystems();
	SubscribeToBarrageEvents();

	SelfPtr = this;

	// Start sim thread on next tick, NOT here.
	// All actors must complete BeginPlay (body + constraint creation) first —
	// Jolt's AddConstraint is NOT safe during PhysicsSystem::Update().
	GetWorld()->GetTimerManager().SetTimerForNextTick([this]()
	{
		StartSimulationThread();
		UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: Simulation thread started (deferred to first tick)"));
	});
}

void UFlecsArtillerySubsystem::Deinitialize()
{
	UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: Deinitializing..."));

	// Stop simulation thread first — guarantees no more DrainCommandQueue/ProgressWorld calls.
	StopSimulationThread();

	// Unsubscribe from Barrage events
	if (ContactEventHandle.IsValid())
	{
		if (CachedBarrageDispatch)
		{
			CachedBarrageDispatch->OnBarrageContactAddedDelegate.Remove(ContactEventHandle);
		}
		ContactEventHandle.Reset();
	}

	CachedBarrageDispatch = nullptr;
	SelfPtr = nullptr;

	delete DebrisPool;
	DebrisPool = nullptr;
	LateSyncBridge.Reset();
	FlecsWorld.Reset();

	Super::Deinitialize();
	UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: Offline"));
}

// ═══════════════════════════════════════════════════════════════
// SIMULATION THREAD MANAGEMENT
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::StartSimulationThread()
{
	SimWorker.BarrageDispatch = CachedBarrageDispatch;
	SimWorker.FlecsSubsystem = this;
	SimThread = FRunnableThread::Create(&SimWorker, TEXT("SIMULATION_THREAD"));
	UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: Simulation thread started"));
}

void UFlecsArtillerySubsystem::StopSimulationThread()
{
	if (SimThread)
	{
		SimThread->Kill(true); // Calls SimWorker.Stop(), then waits for Run() to return
		delete SimThread;
		SimThread = nullptr;
		UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: Simulation thread stopped"));
	}
}

// ═══════════════════════════════════════════════════════════════
// COMMAND QUEUE (Thread-Safe)
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::EnqueueCommand(TFunction<void()>&& Command)
{
	CommandQueue.Enqueue(MoveTemp(Command));
}

// ═══════════════════════════════════════════════════════════════
// GAME THREAD TICK (UTickableWorldSubsystem)
// Controls update ordering for ISM rendering:
//   1. UpdateTransforms — sync existing ISMs to latest physics positions
//   2. ProcessPendingProjectileSpawns — add new ISMs at correct game-thread positions
// This guarantees new projectile ISMs are never overwritten by stale physics data.
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::Tick(float DeltaTime)
{
	// Compute interpolation alpha from sim timing.
	// Alpha ∈ [0,1]: 0 = right after last sim tick, 1 = right before next.
	// Smooth rendering at any FPS by lerping between two sim-tick states.
	float Alpha = 1.0f;
	const uint64 SimTick = SimWorker.SimTickCount.load(std::memory_order_acquire);
	const float SimDt = SimWorker.LastSimDeltaTime.load(std::memory_order_acquire);
	const double LastSimTime = SimWorker.LastSimTickTimeSeconds.load(std::memory_order_acquire);

	if (SimDt > 0.0f && LastSimTime > 0.0)
	{
		const double TimeSince = FPlatformTime::Seconds() - LastSimTime;
		if (TimeSince >= 0.0)
		{
			Alpha = FMath::Clamp(static_cast<float>(TimeSince / SimDt), 0.0f, 1.0f);
		}
	}

	// Cache for consumers (AFlecsCharacter::Tick reads these).
	// IMPORTANT: Character Tick runs BEFORE Subsystem Tick (TG_PrePhysics vs TickableWorldSubsystem).
	// Character reads CachedAlpha + CachedSimTick from the PREVIOUS frame — must be a consistent pair.
	CachedAlpha = Alpha;
	CachedSimTick = SimTick;

	// Step 1: Interpolate all existing ISM transforms between sim-tick states.
	// RenderManager is NOT self-ticking — we drive it here for guaranteed ordering.
	if (UFlecsRenderManager* Renderer = GetRenderManager())
	{
		Renderer->UpdateTransforms(Alpha, SimTick);
	}

	// Step 2: Update Niagara VFX arrays (positions/velocities from physics).
	if (UFlecsNiagaraManager* NiagaraMgr = GetNiagaraManager())
	{
		NiagaraMgr->ProcessPendingRegistrations();
		NiagaraMgr->ProcessPendingRemovals();
		NiagaraMgr->UpdateEffects();
		NiagaraMgr->ProcessPendingDeathEffects();
	}

	// Step 3: Add new ISM instances for projectiles fired since last tick.
	// Uses inverted flow: game-thread recomputes position from fresh camera + raw offset.
	// Since UpdateTransforms already ran, new positions won't be overwritten.
	ProcessPendingProjectileSpawns();

	// Step 4: Add new ISM instances for fragments from destroyed objects.
	ProcessPendingFragmentSpawns();
}

// ═══════════════════════════════════════════════════════════════
// FRAGMENT SPAWN PROCESSING (Game Thread)
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::ProcessPendingFragmentSpawns()
{
	UFlecsRenderManager* Renderer = GetRenderManager();
	if (!Renderer)
	{
		return;
	}

	FPendingFragmentSpawn Spawn;
	while (PendingFragmentSpawns.Dequeue(Spawn))
	{
		if (!Spawn.Mesh || !Spawn.EntityKey.IsValid())
		{
			continue;
		}

		Renderer->AddInstance(Spawn.Mesh, Spawn.Material, Spawn.WorldTransform, Spawn.EntityKey);
	}
}
