// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Artillery-Flecs Bridge: Runs Flecs ECS world on the Artillery 120Hz thread.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ArtilleryActorControllerConcepts.h"
#include "SkeletonTypes.h"
#include "ORDIN.h"
#include "flecs.h"
#include "HAL/ThreadSafeBool.h"
#include <atomic>
#include "FlecsArtillerySubsystem.generated.h"

class UBarrageDispatch;
class UArtilleryDispatch;
struct BarrageContactEvent;

/**
 * Bridge subsystem that runs the Flecs ECS world on the Artillery busy worker thread (~120Hz).
 *
 * This subsystem:
 * - Owns the tick lifecycle of the Flecs world (calls world.progress from ArtilleryTick)
 * - Provides a thread-safe command queue for game thread -> artillery thread operations
 * - Provides LOCK-FREE bidirectional binding between Flecs entities and Barrage physics bodies
 *
 * BIDIRECTIONAL BINDING ARCHITECTURE (Lock-Free):
 * ═══════════════════════════════════════════════════════════════════════════════════════
 * Forward lookup (Entity → BarrageKey):
 *   - Flecs sparse set: entity.get<FBarrageBody>()->BarrageKey  [O(1)]
 *
 * Reverse lookup (BarrageKey → Entity):
 *   - libcuckoo map:    UBarrageDispatch::GetShapeRef(Key)      [O(1)]
 *   - atomic load:      FBLet->GetFlecsEntity()                 [O(1)]
 *
 * Both directions are lock-free. No mutexes, no RWLocks.
 * Thread safety via atomics (FBarragePrimitive::FlecsEntityId) and
 * concurrent data structures (libcuckoo, Flecs sparse set).
 * ═══════════════════════════════════════════════════════════════════════════════════════
 *
 * FLECS STAGES (for future multi-threaded collision processing):
 * Stages are thread-local command queues. Each thread uses its own stage to buffer
 * commands (add/remove/set), which are merged atomically during sync points.
 * Currently all collision processing runs on Artillery thread (stage 0).
 * Future: Barrage collision threads will use their own stages.
 *
 * IMPORTANT: The Flecs world must NOT have any auto-progressing game loops imported.
 * All progress is driven exclusively from ArtilleryTick on the busy worker thread.
 */
UCLASS()
class UFlecsArtillerySubsystem : public UTickableWorldSubsystem, public ISkeletonLord, public ITickHeavy
{
	GENERATED_BODY()

	using ICanReady = ITickHeavy;

public:
	static inline UFlecsArtillerySubsystem* SelfPtr = nullptr;
	constexpr static int OrdinateSeqKey = ORDIN::E_D_C::FlecsSystem;

	/** Max number of Flecs stages (for future multi-threaded collision processing). */
	static constexpr int32 MaxStages = 8;

	// ═══════════════════════════════════════════════════════════════
	// ITickHeavy - Called on Artillery busy worker thread (~120Hz)
	// ═══════════════════════════════════════════════════════════════

	virtual void ArtilleryTick() override;
	virtual bool RegistrationImplementation() override;

	// ═══════════════════════════════════════════════════════════════
	// THREAD-SAFE API (callable from any thread)
	// ═══════════════════════════════════════════════════════════════

	/** Queue a command to execute on the Artillery thread next tick. MPSC-safe. */
	void EnqueueCommand(TFunction<void()>&& Command);

	// ═══════════════════════════════════════════════════════════════
	// BIDIRECTIONAL BINDING API (Artillery thread only)
	// Lock-free O(1) lookups in both directions.
	// ═══════════════════════════════════════════════════════════════

	/** Get the Flecs world. Only safe from Artillery thread. */
	flecs::world* GetFlecsWorld() const { return FlecsWorld.Get(); }

	/**
	 * Get a Flecs stage for thread-safe deferred commands.
	 * @param ThreadIndex Thread index (0 = main Artillery thread). Use 0 for now.
	 * @return Flecs stage for deferred command buffering.
	 */
	flecs::world GetStage(int32 ThreadIndex = 0) const;

	/**
	 * Bind a Flecs entity to a Barrage physics body (bidirectional).
	 * Sets FBarrageBody component on entity AND atomic FlecsEntityId in FBarragePrimitive.
	 * @param Entity The Flecs entity to bind.
	 * @param BarrageKey The SkeletonKey of the Barrage body.
	 */
	void BindEntityToBarrage(flecs::entity Entity, FSkeletonKey BarrageKey);

	/**
	 * Unbind a Flecs entity from its Barrage physics body.
	 * Clears both forward (FBarrageBody component) and reverse (FBarragePrimitive atomic) bindings.
	 * @param Entity The Flecs entity to unbind.
	 */
	void UnbindEntityFromBarrage(flecs::entity Entity);

	/**
	 * Get Flecs entity for a BarrageKey. O(1) lock-free via atomic in FBarragePrimitive.
	 * @param BarrageKey The SkeletonKey to look up.
	 * @return Flecs entity (check is_valid()) or invalid entity if not bound.
	 */
	flecs::entity GetEntityForBarrageKey(FSkeletonKey BarrageKey) const;

	/**
	 * Get BarrageKey for a Flecs entity. O(1) via Flecs sparse set.
	 * @param Entity The Flecs entity to look up.
	 * @return FSkeletonKey or invalid key if not bound.
	 */
	FSkeletonKey GetBarrageKeyForEntity(flecs::entity Entity) const;

	/**
	 * Check if a BarrageKey has a bound Flecs entity. O(1) lock-free.
	 * @param BarrageKey The SkeletonKey to check.
	 * @return true if bound to a Flecs entity.
	 */
	bool HasEntityForBarrageKey(FSkeletonKey BarrageKey) const;

	// ═══════════════════════════════════════════════════════════════
	// DEPRECATED API (for backward compatibility during migration)
	// Will be removed after FlecsGameplayLibrary and FlecsCharacter are updated.
	// ═══════════════════════════════════════════════════════════════

	UE_DEPRECATED(5.7, "Use BindEntityToBarrage instead")
	void RegisterBarrageEntity(FSkeletonKey BarrageKey, uint64 FlecsEntityId);

	UE_DEPRECATED(5.7, "Use UnbindEntityFromBarrage instead")
	void UnregisterBarrageEntity(FSkeletonKey BarrageKey);

	UE_DEPRECATED(5.7, "Use GetEntityForBarrageKey instead")
	uint64 GetFlecsEntityForBarrageKey(FSkeletonKey BarrageKey) const;

	UE_DEPRECATED(5.7, "Use HasEntityForBarrageKey instead")
	bool HasFlecsEntityForBarrageKey(FSkeletonKey BarrageKey) const;

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

public:
	virtual void PostInitialize() override;
	TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UFlecsArtillerySubsystem, STATGROUP_Tickables);
	}

private:
	/** Set up Flecs systems that run on the Artillery thread. */
	void SetupFlecsSystems();

	/** Subscribe to Barrage collision events. */
	void SubscribeToBarrageEvents();

	/** Handle collision event from Barrage. Called on Artillery thread. */
	void OnBarrageContact(const BarrageContactEvent& Event);

	/** Direct flecs::world - no UFlecsWorld wrapper, no plugin tick functions, no worker threads. */
	TUniquePtr<flecs::world> FlecsWorld;

	/** MPSC command queue: game thread producers -> artillery thread consumer. */
	TQueue<TFunction<void()>, EQueueMode::Mpsc> CommandQueue;

	/** Delegate handle for Barrage contact events (for cleanup). */
	FDelegateHandle ContactEventHandle;

	// ═══════════════════════════════════════════════════════════════
	// CACHED SUBSYSTEM POINTERS (set during RegistrationImplementation)
	// Avoids repeated SelfPtr lookups on hot paths.
	// ═══════════════════════════════════════════════════════════════
	UBarrageDispatch* CachedBarrageDispatch = nullptr;
	UArtilleryDispatch* CachedArtilleryDispatch = nullptr;

	// ═══════════════════════════════════════════════════════════════
	// DEINITIALIZATION SYNCHRONIZATION
	// Prevents use-after-free when Game thread destroys FlecsWorld
	// while Artillery thread is inside ArtilleryTick().
	// ═══════════════════════════════════════════════════════════════

	/** Set to true when Deinitialize() starts. ArtilleryTick() checks this and exits early. */
	std::atomic<bool> bDeinitializing{false};

	/** Set to true while inside ArtilleryTick(). Deinitialize() waits for this to become false. */
	std::atomic<bool> bInArtilleryTick{false};
};
