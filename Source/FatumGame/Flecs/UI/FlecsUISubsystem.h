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

/** Per-entity value shared state. Sim thread writes packed atomics, game thread reads. */
struct FValueSharedState
{
	/** Health: upper 32 = HP bits, lower 32 = MaxHP bits. */
	alignas(64) std::atomic<uint64> HealthPacked{0};

	/** Ammo: upper 16 = CurrentAmmo, next 16 = MagSize, lower 32 = Reserve. */
	alignas(64) std::atomic<uint64> AmmoPacked{0};

	static uint64 PackHealth(float HP, float MaxHP)
	{
		uint32 HPBits, MaxBits;
		FMemory::Memcpy(&HPBits, &HP, sizeof(float));
		FMemory::Memcpy(&MaxBits, &MaxHP, sizeof(float));
		return (static_cast<uint64>(HPBits) << 32) | MaxBits;
	}

	static void UnpackHealth(uint64 Packed, float& HP, float& MaxHP)
	{
		uint32 HPBits = static_cast<uint32>(Packed >> 32);
		uint32 MaxBits = static_cast<uint32>(Packed);
		FMemory::Memcpy(&HP, &HPBits, sizeof(float));
		FMemory::Memcpy(&MaxHP, &MaxBits, sizeof(float));
	}

	static uint64 PackAmmo(int32 Current, int32 MagSize, int32 Reserve)
	{
		uint64 C = static_cast<uint16>(Current);
		uint64 M = static_cast<uint16>(MagSize);
		uint64 R = static_cast<uint32>(Reserve);
		return (C << 48) | (M << 32) | R;
	}

	static void UnpackAmmo(uint64 Packed, int32& Current, int32& MagSize, int32& Reserve)
	{
		Current = static_cast<int32>(static_cast<uint16>(Packed >> 48));
		MagSize = static_cast<int32>(static_cast<uint16>(Packed >> 32));
		Reserve = static_cast<int32>(static_cast<uint32>(Packed));
	}
};

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
		TUniquePtr<FValueSharedState> SharedState;
		int32 RefCount = 0;
	};
	TMap<int64, FValueEntry> Values;

	/** GC roots — prevents garbage collection of model UObjects stored in plain structs. */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> GCRoots;

	UPROPERTY()
	TObjectPtr<UFlecsArtillerySubsystem> Artillery;
};
