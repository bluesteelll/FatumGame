// FlecsNiagaraManager: Drives Niagara VFX for ECS entities via Array Data Interface.
//
// One ANiagaraActor per effect type. Zero per-entity UObjects.
// Positions pushed each frame via SetNiagaraArrayVector.
// NOT self-ticking — driven by UFlecsArtillerySubsystem::Tick() for ordering.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SkeletonTypes.h"
#include "NiagaraSystem.h"
#include "Containers/Queue.h"
#include "FlecsNiagaraManager.generated.h"

class ANiagaraActor;
class UNiagaraComponent;

// ═══════════════════════════════════════════════════════════════
// FLECS COMPONENT (plain struct, NOT USTRUCT)
// Set on entity at spawn time, read by DeadEntityCleanupSystem.
// ═══════════════════════════════════════════════════════════════

struct FNiagaraDeathEffect
{
	UNiagaraSystem* Effect = nullptr;
	float Scale = 1.0f;
};

// Stored at contact time so DeadEntityCleanupSystem uses the IMPACT position,
// not the post-bounce physics position (which is wrong after StepWorld resolves collision).
struct FDeathContactPoint
{
	FVector Position = FVector::ZeroVector;
};

// ═══════════════════════════════════════════════════════════════
// MPSC QUEUE STRUCTS
// ═══════════════════════════════════════════════════════════════

struct FPendingNiagaraRegistration
{
	FSkeletonKey Key;
	UNiagaraSystem* Effect = nullptr;
	float Scale = 1.0f;
	FVector Offset = FVector::ZeroVector;
};

struct FPendingDeathEffect
{
	FVector Location = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	UNiagaraSystem* Effect = nullptr;
	float Scale = 1.0f;
};

/**
 * Manages Niagara VFX for Flecs entities using Array Data Interface pattern.
 *
 * Architecture:
 * - One ANiagaraActor per UNiagaraSystem* (effect type dedup)
 * - Positions/velocities rebuilt each frame from Barrage physics
 * - Pushed to Niagara via SetNiagaraArrayVector
 * - Death effects are fire-and-forget via SpawnSystemAtLocation
 *
 * Thread safety:
 * - RegisterEntity/UnregisterEntity: game thread only
 * - EnqueueRegistration/EnqueueRemoval/EnqueueDeathEffect: MPSC (sim thread safe)
 * - UpdateEffects/ProcessPending*: game thread only (called by ArtillerySubsystem::Tick)
 */
UCLASS()
class FATUMGAME_API UFlecsNiagaraManager : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// REGISTRATION API (game thread only)
	// ═══════════════════════════════════════════════════════════════

	/** Register an entity for attached VFX tracking. Game thread only. */
	void RegisterEntity(FSkeletonKey Key, UNiagaraSystem* Effect, float Scale, FVector Offset);

	/** Unregister an entity from VFX tracking. Game thread only. */
	void UnregisterEntity(FSkeletonKey Key);

	// ═══════════════════════════════════════════════════════════════
	// TICK API (called by UFlecsArtillerySubsystem::Tick)
	// ═══════════════════════════════════════════════════════════════

	/** Read physics positions for all registered entities, push arrays to Niagara. */
	void UpdateEffects();

	/** Drain pending registration queue (sim → game thread). */
	void ProcessPendingRegistrations();

	/** Drain pending removal queue (sim → game thread). */
	void ProcessPendingRemovals();

	/** Drain pending death effect queue, spawn fire-and-forget VFX. */
	void ProcessPendingDeathEffects();

	// ═══════════════════════════════════════════════════════════════
	// MPSC API (sim thread → game thread)
	// ═══════════════════════════════════════════════════════════════

	/** Queue a registration for processing on game thread. Thread-safe. */
	void EnqueueRegistration(const FPendingNiagaraRegistration& Reg);

	/** Queue a removal for processing on game thread. Thread-safe. */
	void EnqueueRemoval(FSkeletonKey Key);

	/** Queue a death effect for spawning on game thread. Thread-safe. */
	void EnqueueDeathEffect(const FPendingDeathEffect& Effect);

	// ═══════════════════════════════════════════════════════════════
	// STATIC ACCESSOR
	// ═══════════════════════════════════════════════════════════════

	static UFlecsNiagaraManager* Get(UWorld* World);

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	// ═══════════════════════════════════════════════════════════════
	// EFFECT GROUP (one per UNiagaraSystem*)
	// ═══════════════════════════════════════════════════════════════

	struct FEffectGroup
	{
		// Raw pointer — lifetime managed by Deinitialize() (Destroy on world teardown).
		// Cannot use UPROPERTY/TObjectPtr: FEffectGroup is not a USTRUCT.
		ANiagaraActor* NiagaraActor = nullptr;

		UNiagaraComponent* NiagaraComponent = nullptr;

		TSet<FSkeletonKey> RegisteredKeys;

		// Scratch arrays — rebuilt each frame, Reset() keeps allocation
		TArray<FVector> Positions;
		TArray<FVector> Velocities;
	};

	/** One effect group per Niagara system type. Keyed on UNiagaraSystem*. */
	TMap<UNiagaraSystem*, FEffectGroup> EffectGroups;

	/** Reverse lookup: entity key → which effect group it belongs to. */
	TMap<FSkeletonKey, UNiagaraSystem*> EntityToEffect;

	// ═══════════════════════════════════════════════════════════════
	// MPSC QUEUES (sim → game thread)
	// ═══════════════════════════════════════════════════════════════

	TQueue<FPendingNiagaraRegistration, EQueueMode::Mpsc> PendingRegistrations;
	TQueue<FSkeletonKey, EQueueMode::Mpsc> PendingRemovals;
	TQueue<FPendingDeathEffect, EQueueMode::Mpsc> PendingDeathEffects;

	// ═══════════════════════════════════════════════════════════════
	// INTERNAL
	// ═══════════════════════════════════════════════════════════════

	/** Get or create an effect group for a Niagara system type. */
	FEffectGroup& GetOrCreateEffectGroup(UNiagaraSystem* Effect);
};
