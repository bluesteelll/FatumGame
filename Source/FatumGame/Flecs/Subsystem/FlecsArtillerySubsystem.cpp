// FlecsArtillerySubsystem - Core Lifecycle and Tick
//
// This file contains:
// - Subsystem lifecycle (Initialize, Deinitialize, etc)
// - ArtilleryTick (main simulation loop)
// - EnqueueCommand (thread-safe command queue)
//
// See also:
// - FlecsArtillerySubsystem_Systems.cpp   - Flecs systems setup
// - FlecsArtillerySubsystem_Collision.cpp - Collision handling
// - FlecsArtillerySubsystem_Binding.cpp   - Bidirectional binding API
// - FlecsArtillerySubsystem_Items.cpp     - Item prefab registry

#include "FlecsArtillerySubsystem.h"
#include "ArtilleryDispatch.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FlecsGameTags.h"

// ═══════════════════════════════════════════════════════════════
// REGISTRATION (ITickHeavy)
// ═══════════════════════════════════════════════════════════════

bool UFlecsArtillerySubsystem::RegistrationImplementation()
{
	// Create our own flecs::world directly - no plugin involvement, no tick functions, no worker threads.
	// This world runs ONLY on the Artillery thread via ArtilleryTick().
	FlecsWorld = MakeUnique<flecs::world>();

	if (!FlecsWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("FlecsArtillerySubsystem: Failed to create Flecs world. Flecs ECS will not run."));
		return false;
	}

	// CRITICAL: Disable Flecs' internal threading - Artillery thread is our only executor.
	FlecsWorld->set_threads(0);

	// Cache subsystem pointers to avoid repeated SelfPtr lookups on hot paths.
	CachedBarrageDispatch = UBarrageDispatch::SelfPtr;
	CachedArtilleryDispatch = UArtilleryDispatch::SelfPtr;

	if (!CachedBarrageDispatch)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: UBarrageDispatch not available during registration"));
	}

	// Create Flecs systems that run each tick on the Artillery thread.
	SetupFlecsSystems();

	// Subscribe to Barrage collision events (runs on Artillery thread).
	SubscribeToBarrageEvents();

	// Register ourselves with ArtilleryDispatch so the busy worker calls our ArtilleryTick.
	if (CachedArtilleryDispatch)
	{
		CachedArtilleryDispatch->SetFlecsDispatch(this);
	}
	SelfPtr = this;

	UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: Online (lock-free bidirectional binding, %d stages available)"), MaxStages);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// ARTILLERY TICK (ITickHeavy - ~120Hz)
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::ArtilleryTick()
{
	// CRITICAL: Check if deinitializing. If so, exit immediately without touching FlecsWorld.
	if (bDeinitializing.load(std::memory_order_acquire))
	{
		return;
	}

	// Mark that we're inside ArtilleryTick. Deinitialize() will wait for this to become false.
	bInArtilleryTick.store(true, std::memory_order_release);

	// Double-check after setting flag (handles race where Deinitialize started between checks)
	if (bDeinitializing.load(std::memory_order_acquire) || !FlecsWorld)
	{
		bInArtilleryTick.store(false, std::memory_order_release);
		return;
	}

	// Drain the command queue. All mutations from the game thread are applied here,
	// on the artillery thread, before Flecs systems run.
	TFunction<void()> Command;
	while (CommandQueue.Dequeue(Command))
	{
		if (bDeinitializing.load(std::memory_order_acquire))
		{
			bInArtilleryTick.store(false, std::memory_order_release);
			return;
		}
		Command();
	}

	// Final check before expensive progress() call
	if (bDeinitializing.load(std::memory_order_acquire))
	{
		bInArtilleryTick.store(false, std::memory_order_release);
		return;
	}

	// Progress the Flecs world. This runs all registered Flecs systems.
	constexpr double ArtilleryDeltaTime = 1.0 / 120.0;
	FlecsWorld->progress(ArtilleryDeltaTime);

	bInArtilleryTick.store(false, std::memory_order_release);
}

// ═══════════════════════════════════════════════════════════════
// SUBSYSTEM LIFECYCLE
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Declare dependencies BEFORE Super::Initialize() for correct deinitialization order.
	Collection.InitializeDependency<UArtilleryDispatch>();
	Collection.InitializeDependency<UBarrageDispatch>();

	Super::Initialize(Collection);
	SET_INITIALIZATION_ORDER_BY_ORDINATEKEY_AND_WORLD
}

void UFlecsArtillerySubsystem::PostInitialize()
{
	Super::PostInitialize();
}

void UFlecsArtillerySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
}

void UFlecsArtillerySubsystem::Deinitialize()
{
	UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: Deinitializing..."));

	// ═══════════════════════════════════════════════════════════════
	// STEP 1: Signal ArtilleryTick() to stop and exit early.
	// ═══════════════════════════════════════════════════════════════
	bDeinitializing.store(true, std::memory_order_release);

	// ═══════════════════════════════════════════════════════════════
	// STEP 2: Clear our reference in Artillery to prevent NEW calls.
	// ═══════════════════════════════════════════════════════════════
	if (CachedArtilleryDispatch)
	{
		CachedArtilleryDispatch->SetFlecsDispatch(nullptr);
		UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: Cleared FlecsDispatch pointer in Artillery"));
	}

	// ═══════════════════════════════════════════════════════════════
	// STEP 3: Wait for any in-flight ArtilleryTick() to complete.
	// ═══════════════════════════════════════════════════════════════
	constexpr int32 MaxSpinIterations = 1000; // ~100ms with 0.1ms sleep
	int32 SpinCount = 0;
	while (bInArtilleryTick.load(std::memory_order_acquire))
	{
		if (++SpinCount > MaxSpinIterations)
		{
			UE_LOG(LogTemp, Error, TEXT("FlecsArtillerySubsystem: Timeout waiting for ArtilleryTick to exit!"));
			break;
		}
		FPlatformProcess::Sleep(0.0001f);
	}

	if (SpinCount > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: Waited %d iterations for ArtilleryTick to exit"), SpinCount);
	}

	// ═══════════════════════════════════════════════════════════════
	// STEP 4: Safe cleanup - ArtilleryTick is guaranteed to not be running.
	// ═══════════════════════════════════════════════════════════════

	// Unsubscribe from Barrage events
	if (ContactEventHandle.IsValid())
	{
		if (CachedBarrageDispatch)
		{
			CachedBarrageDispatch->OnBarrageContactAddedDelegate.Remove(ContactEventHandle);
		}
		ContactEventHandle.Reset();
	}

	// Clear cached pointers
	CachedBarrageDispatch = nullptr;
	CachedArtilleryDispatch = nullptr;

	SelfPtr = nullptr;

	// NOW safe to destroy FlecsWorld
	FlecsWorld.Reset();

	Super::Deinitialize();
}

// ═══════════════════════════════════════════════════════════════
// COMMAND QUEUE (Thread-Safe)
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::EnqueueCommand(TFunction<void()>&& Command)
{
	CommandQueue.Enqueue(MoveTemp(Command));
}
