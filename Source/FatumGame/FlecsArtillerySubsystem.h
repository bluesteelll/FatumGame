// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Artillery-Flecs Bridge: Runs Flecs ECS world on the Artillery 120Hz thread.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ArtilleryActorControllerConcepts.h"
#include "SkeletonTypes.h"
#include "ORDIN.h"
#include "flecs.h"
#include "FlecsArtillerySubsystem.generated.h"

class UBarrageDispatch;
struct BarrageContactEvent;

/**
 * Bridge subsystem that runs the Flecs ECS world on the Artillery busy worker thread (~120Hz).
 *
 * This subsystem:
 * - Owns the tick lifecycle of the Flecs world (calls world.progress from ArtilleryTick)
 * - Provides a thread-safe command queue for game thread -> artillery thread operations
 * - Maintains a BarrageKey -> Flecs entity index for collision lookups
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
	// ARTILLERY THREAD API (only safe from Artillery busy worker)
	// ═══════════════════════════════════════════════════════════════

	/** Get the Flecs world. Only safe from Artillery thread. */
	flecs::world* GetFlecsWorld() const { return FlecsWorld.Get(); }

	/** Register a mapping from SkeletonKey to Flecs entity ID (for collision lookups). */
	void RegisterBarrageEntity(FSkeletonKey BarrageKey, uint64 FlecsEntityId);

	/** Remove a SkeletonKey -> Flecs entity mapping. */
	void UnregisterBarrageEntity(FSkeletonKey BarrageKey);

	/** Look up the Flecs entity ID for a given SkeletonKey. Returns 0 if not found. */
	uint64 GetFlecsEntityForBarrageKey(FSkeletonKey BarrageKey) const;

	/** Check if a SkeletonKey has a registered Flecs entity. */
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

	/** SkeletonKey.Obj -> flecs entity_t. Only accessed from Artillery thread. */
	TMap<uint64, uint64> BarrageKeyIndex;

	/** Delegate handle for Barrage contact events (for cleanup). */
	FDelegateHandle ContactEventHandle;
};
