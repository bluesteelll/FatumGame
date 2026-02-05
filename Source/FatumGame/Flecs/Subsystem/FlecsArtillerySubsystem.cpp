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

	// Create Flecs world
	FlecsWorld = MakeUnique<flecs::world>();
	checkf(FlecsWorld.IsValid(), TEXT("FlecsArtillerySubsystem: Failed to create Flecs world"));

	int32 NumWorkers = FMath::Max(2, FPlatformMisc::NumberOfCoresIncludingHyperthreads() - 2);
	FlecsWorld->set_threads(NumWorkers);
	UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: %d Flecs worker threads enabled"), NumWorkers);

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
// Processes operations that require Game thread (e.g., Barrage body creation).
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::Tick(float DeltaTime)
{
	// Process pending projectile spawns from weapon fire.
	// These are queued by WeaponFireSystem on simulation thread.
	ProcessPendingProjectileSpawns();
}
