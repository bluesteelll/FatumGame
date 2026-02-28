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
	check(Character->CachedBarrageBody.IsValid());
	check(Character->InputAtomics.IsValid());

	FCharacterPhysBridge Bridge;
	Bridge.CachedBody = Character->CachedBarrageBody;
	Bridge.InputAtomics = Character->InputAtomics;
	Bridge.CharacterKey = Character->CharacterKey;
	Bridge.SlideActive = Character->SlideActiveAtomic;
	Bridge.MantleActive = Character->MantleActiveAtomic;
	Bridge.Hanging = Character->HangingAtomic;

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
	checkf(Bridge.CachedFBChar, TEXT("RegisterCharacterBridge: Failed to resolve FBCharacter for key %llu"),
		static_cast<uint64>(Character->CharacterKey));

	CharacterBridges.Add(MoveTemp(Bridge));
}

void UFlecsArtillerySubsystem::UnregisterCharacterBridge(FSkeletonKey CharacterKey)
{
	CharacterBridges.RemoveAll([CharacterKey](const FCharacterPhysBridge& B) { return B.CharacterKey == CharacterKey; });
}

// ═══════════════════════════════════════════════════════════════
// FRESH ALPHA COMPUTATION (for AFlecsCharacter::Tick, runs before subsystem Tick)
// ═══════════════════════════════════════════════════════════════

float UFlecsArtillerySubsystem::ComputeFreshAlpha(uint64& OutSimTick) const
{
	OutSimTick = SimWorker.SimTickCount.load(std::memory_order_acquire);
	const float SimDt = SimWorker.LastSimDeltaTime.load(std::memory_order_acquire);
	const double LastSimTime = SimWorker.LastSimTickTimeSeconds.load(std::memory_order_acquire);

	if (SimDt > 0.0f && LastSimTime > 0.0)
	{
		const double TimeSince = FPlatformTime::Seconds() - LastSimTime;
		if (TimeSince >= 0.0)
		{
			return FMath::Clamp(static_cast<float>(TimeSince / SimDt), 0.0f, 1.0f);
		}
	}
	return 1.0f;
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

		// 2.5. Mantle/Vault/LedgeGrab position control (sim-thread owned)
		FMantleInstance* Mantle = Bridge.Entity.try_get_mut<FMantleInstance>();
		bool bMantling = false;
		bool bHanging = false;
		if (Mantle)
		{
			bMantling = true;
			Mantle->Timer += DeltaTime;

			if (Mantle->Phase == 4) // Hanging
			{
				bHanging = true;
				// Pin position — override CharacterVirtual to hang position
				JPH::Vec3 HangPos(Mantle->EndX, Mantle->EndY, Mantle->EndZ);
				FBChar->mCharacter->SetPosition(HangPos);
				FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
				// Zero velocity to prevent gravity accumulation during hang.
				FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());

				// Check timeout (LedgeGrabMaxDuration=0 means infinite)
				float MaxDur = MS->LedgeGrabMaxDuration;
				if (MaxDur > 0.f && Mantle->Timer > MaxDur)
				{
					Bridge.Entity.remove<FMantleInstance>();
					bMantling = false;
					bHanging = false;
				}
			}
			else if (Mantle->Phase <= 3) // GrabTransition, Rise, Pull, Land
			{
				float Alpha = FMath::Clamp(Mantle->Timer / FMath::Max(Mantle->PhaseDuration, 0.001f), 0.f, 1.f);

				// Ease curves: Rise=EaseOut, Pull=EaseInOut, GrabTransition=EaseInOut, Land=linear
				float EasedAlpha;
				if (Mantle->Phase == 1) // Rise
					EasedAlpha = 1.f - (1.f - Alpha) * (1.f - Alpha); // EaseOut quadratic
				else if (Mantle->Phase == 0 || Mantle->Phase == 2) // GrabTransition or Pull
					EasedAlpha = Alpha * Alpha * (3.f - 2.f * Alpha); // Smoothstep
				else
					EasedAlpha = Alpha; // Land: linear

				// Lerp position
				JPH::Vec3 Start(Mantle->StartX, Mantle->StartY, Mantle->StartZ);
				JPH::Vec3 End(Mantle->EndX, Mantle->EndY, Mantle->EndZ);
				JPH::Vec3 LerpedPos = Start + (End - Start) * EasedAlpha;
				FBChar->mCharacter->SetPosition(LerpedPos);
				FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
				// Zero velocity during lerp phases too
				FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());

				// Phase completion
				if (Mantle->Timer >= Mantle->PhaseDuration)
				{
					Mantle->Timer = 0.f;

					// GrabTransition → Hanging (skip Rise/Pull/Land for initial LedgeGrab)
					if (Mantle->Phase == 0 && Mantle->MantleType == 2)
					{
						Mantle->Phase = 4; // Jump directly to Hanging
						bHanging = true;
					}
					else
					{
						Mantle->Phase++;

						if (Mantle->Phase == 2) // Rise→Pull transition
						{
							Mantle->StartX = Mantle->EndX;
							Mantle->StartY = Mantle->EndY;
							Mantle->StartZ = Mantle->EndZ;
							Mantle->EndX = Mantle->PullEndX;
							Mantle->EndY = Mantle->PullEndY;
							Mantle->EndZ = Mantle->PullEndZ;
							Mantle->PhaseDuration = Mantle->PullDuration;
						}
						else if (Mantle->Phase == 3) // Pull→Land (hold at pull-end position)
						{
							Mantle->StartX = Mantle->PullEndX; Mantle->StartY = Mantle->PullEndY; Mantle->StartZ = Mantle->PullEndZ;
							Mantle->EndX   = Mantle->PullEndX; Mantle->EndY   = Mantle->PullEndY; Mantle->EndZ   = Mantle->PullEndZ;
							Mantle->PhaseDuration = Mantle->LandDuration;
						}
						else if (Mantle->Phase == 4) // Land→complete or Hanging
						{
							if (Mantle->MantleType == 2) // LedgeGrab after pull-up → re-hang
							{
								bHanging = true;
								Mantle->Timer = 0.f;
							}
							else // Vault/Mantle complete
							{
								Bridge.Entity.remove<FMantleInstance>();
								bMantling = false;
							}
						}
						else if (Mantle->Phase > 4)
						{
							Bridge.Entity.remove<FMantleInstance>();
							bMantling = false;
						}
					}
				}
			}
		}
		Bridge.MantleActive->store(bMantling, std::memory_order_relaxed);
		Bridge.Hanging->store(bHanging, std::memory_order_relaxed);

		if (bMantling)
		{
			Bridge.SlideActive->store(false, std::memory_order_relaxed);
			continue; // skip normal locomotion
		}

		// 3. Slide deceleration (sim-thread owned)
		FSlideInstance* Slide = Bridge.Entity.try_get_mut<FSlideInstance>();
		bool bSliding = false;
		if (Slide)
		{
			// Capture slide direction on first tick (from current Jolt velocity)
			if (Slide->SlideDirX == 0.f && Slide->SlideDirZ == 0.f)
			{
				JPH::Vec3 CurVel = FBChar->mCharacter->GetLinearVelocity();
				float HorizLen = FMath::Sqrt(CurVel.GetX() * CurVel.GetX() + CurVel.GetZ() * CurVel.GetZ());
				if (HorizLen > 0.01f)
				{
					Slide->SlideDirX = CurVel.GetX() / HorizLen;
					Slide->SlideDirZ = CurVel.GetZ() / HorizLen;
				}
			}

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
		Bridge.SlideActive->store(bSliding, std::memory_order_relaxed);

		// 4. Slide: direct velocity control (bypasses MoveTowards — slide owns deceleration)
		if (bSliding)
		{
			float SpeedJolt = Slide->CurrentSpeed / 100.f;
			JPH::Vec3 SlideVel(Slide->SlideDirX * SpeedJolt, 0, Slide->SlideDirZ * SpeedJolt);

			// Minor steering from input
			float SlideDirInputLen = FMath::Sqrt(DirX * DirX + DirZ * DirZ);
			if (SlideDirInputLen > 0.01f)
			{
				float SteerRate = MS->SlideMinAcceleration / 100.f;
				float InvDirLen = 1.f / SlideDirInputLen;
				JPH::Vec3 SteerTarget(DirX * InvDirLen * SpeedJolt, 0, DirZ * InvDirLen * SpeedJolt);
				JPH::Vec3 SteerDiff = SteerTarget - SlideVel;
				float SteerLen = SteerDiff.Length();
				float SteerStep = SteerRate * DeltaTime;
				if (SteerLen > SteerStep && SteerLen > 1.0e-6f)
				{
					SlideVel = SlideVel + (SteerDiff / SteerLen) * SteerStep;
				}
			}

			FBChar->mLocomotionUpdate = SlideVel;
			continue;
		}

		// 5. Compute target speed from posture/sprint/slide
		float TargetSpeedCm;
		float AccelCm;
		if (State && State->bSprinting && State->Posture == 0)
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

		// 6. Ground state from Jolt CharacterVirtual
		bool bOnGround = (FBChar->mCharacter->GetGroundState()
			== JPH::CharacterVirtual::EGroundState::OnGround);

		// 7. Deceleration + air accel (cm/s^2)
		float DecelCm = MS->GroundDeceleration;
		float AirAccelCm = MS->AirAcceleration;

		// 8. Build target horizontal velocity (UE cm/s → Jolt m/s)
		float DirLen = FMath::Sqrt(DirX * DirX + DirZ * DirZ);
		JPH::Vec3 TargetH = JPH::Vec3::sZero();
		if (DirLen > 0.01f)
		{
			float InvDirLen = 1.f / DirLen;
			float SpeedJolt = TargetSpeedCm / 100.f;  // cm→m
			TargetH = JPH::Vec3(DirX * InvDirLen * SpeedJolt, 0, DirZ * InvDirLen * SpeedJolt);
		}

		// 9. Read current horizontal from CharacterVirtual
		JPH::Vec3 CurVelo = FBChar->mCharacter->GetLinearVelocity();
		JPH::Vec3 CurH(CurVelo.GetX(), 0, CurVelo.GetZ());

		// 10. Pick accel rate (m/s^2)
		float AccelRate;
		if (bOnGround)
			AccelRate = TargetH.IsNearZero() ? (DecelCm / 100.f) : (AccelCm / 100.f);
		else
			AccelRate = AirAccelCm / 100.f;

		// 11. MoveTowards: smooth current → target
		JPH::Vec3 Diff = TargetH - CurH;
		float DiffLen = Diff.Length();
		float Step = AccelRate * DeltaTime;
		JPH::Vec3 SmoothedH;
		if (DiffLen <= Step || DiffLen < 1.0e-6f)
			SmoothedH = TargetH;
		else
			SmoothedH = CurH + (Diff / DiffLen) * Step;

		// 12. Write pre-smoothed velocity to FBCharacter (consumed by StepCharacter)
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

	// Step 2: Character positions are updated in AFlecsCharacter::Tick (TG_PrePhysics)
	// — must run before CameraManager (TG_PostPhysics) to avoid 1-frame camera lag.

	// Step 3: Update Niagara VFX arrays (positions/velocities from physics).
	if (UFlecsNiagaraManager* NiagaraMgr = GetNiagaraManager())
	{
		NiagaraMgr->ProcessPendingRegistrations();
		NiagaraMgr->ProcessPendingRemovals();
		NiagaraMgr->UpdateEffects();
		NiagaraMgr->ProcessPendingDeathEffects();
	}

	// Step 4: Add new ISM instances for projectiles fired since last tick.
	// Uses inverted flow: game-thread recomputes position from fresh camera + raw offset.
	// Since UpdateTransforms already ran, new positions won't be overwritten.
	ProcessPendingProjectileSpawns();

	// Step 5: Add new ISM instances for fragments from destroyed objects.
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
