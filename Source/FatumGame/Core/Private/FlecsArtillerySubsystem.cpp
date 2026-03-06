// FlecsArtillerySubsystem - Core Lifecycle, Simulation Thread, and Tick
//
// This file contains:
// - Subsystem lifecycle (Initialize, OnWorldBeginPlay, Deinitialize)
// - Simulation thread management (Start/Stop)
// - DrainCommandQueue + ProgressWorld (called by FSimulationWorker)
// - EnqueueCommand (thread-safe command queue)
// - Tick (interpolation alpha + ISM update + projectile spawn processing)
//
// See also:
// - FlecsArtillerySubsystem_Character.cpp - Character physics bridge
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
	// Compute interpolation alpha from sim timing (reuses ComputeFreshAlpha).
	// IMPORTANT: Character Tick runs BEFORE Subsystem Tick (TG_PrePhysics vs TickableWorldSubsystem).
	// Character uses ComputeFreshAlpha() directly for a fresh wall-clock read.
	// Here we cache the result for ISM UpdateTransforms and other consumers.
	CachedAlpha = ComputeFreshAlpha(CachedSimTick);
	float Alpha = CachedAlpha;

	// Step 1: Interpolate all existing ISM transforms between sim-tick states.
	// RenderManager is NOT self-ticking — we drive it here for guaranteed ordering.
	if (UFlecsRenderManager* Renderer = GetRenderManager())
	{
		Renderer->UpdateTransforms(Alpha, CachedSimTick);
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
