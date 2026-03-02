// UFlecsUISubsystem — game module subsystem bridging FlecsUI models to sim thread.
// Owns shared memory (triple buffers, atomics, MPSC queues).
// Tick: O(N) atomic loads per active model, zero sim thread interaction.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Containers/TripleBuffer.h"
#include "FlecsContainerTypes.h"
#include "FlecsUITypes.h"
#include <atomic>
#include "FlecsUISubsystem.generated.h"

class UFlecsContainerModel;
class UFlecsValueModel;
class UFlecsArtillerySubsystem;

// ═══════════════════════════════════════════════════════════════
// SHARED MEMORY STRUCTS (lock-free, game module owns)
// ═══════════════════════════════════════════════════════════════

/** Per-container shared state. Sim thread writes, game thread reads. */
struct FContainerSharedState
{
	/** Sim → Game: latest container snapshot (lock-free triple buffer). */
	TTripleBuffer<FContainerSnapshot> SnapshotBuffer;

	/** Sim → Game: version counter (1 atomic load for dirty check). */
	alignas(64) std::atomic<uint32> SimVersion{0};

	/** Game thread only: last seen version (no atomic needed). */
	uint32 GameSeenVersion = 0;

	/** Sim → Game: op results (lock-free MPSC, must-deliver). */
	TQueue<FOpResult, EQueueMode::Mpsc> OpResults;
};

// FValueSharedState removed — packing utilities moved to FSimStateCache.h (SimStatePacking namespace).
// Value models now read from FSimStateCache via UFlecsArtillerySubsystem.

// ═══════════════════════════════════════════════════════════════
// SUBSYSTEM
// ═══════════════════════════════════════════════════════════════

UCLASS()
class FATUMGAME_API UFlecsUISubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// ═══ Singleton (like other subsystems in this project) ═══
	static inline UFlecsUISubsystem* SelfPtr = nullptr;

	// ═══ Model Factory ═══

	/** Acquire a container model for the given container entity. Creates if needed, ref-counted. */
	UFlecsContainerModel* AcquireContainerModel(int64 ContainerEntityId);

	/** Acquire a value model for the given entity. Creates if needed, ref-counted. */
	UFlecsValueModel* AcquireValueModel(int64 EntityId);

	/** Release a model. Destroys when ref count reaches zero. */
	void ReleaseModel(int64 EntityId);

	/** Find shared state for a container (for external mutation detection). Returns nullptr if not tracked. */
	FContainerSharedState* FindContainerSharedState(int64 ContainerEntityId);

	/** Build a container snapshot from ECS. Called on sim thread. */
	FContainerSnapshot BuildContainerSnapshot(int64 ContainerEntityId);

	// ═══ Tickable ═══
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	// ═══ Bridge Setup ═══
	void SetupContainerBridge(int64 ContainerId);
	void SetupValueBridge(int64 EntityId);

	// ═══ Per-Container Entry ═══
	struct FContainerEntry
	{
		TObjectPtr<UFlecsContainerModel> Model;
		TUniquePtr<FContainerSharedState> SharedState;
		int32 RefCount = 0;
	};
	TMap<int64, FContainerEntry> Containers;

	// ═══ Per-Entity Value Entry ═══
	struct FValueEntry
	{
		TObjectPtr<UFlecsValueModel> Model;
		int32 RefCount = 0;
	};
	TMap<int64, FValueEntry> Values;

	/** GC roots — prevents garbage collection of model UObjects stored in plain structs. */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> GCRoots;

	UPROPERTY()
	TObjectPtr<UFlecsArtillerySubsystem> Artillery;
};
