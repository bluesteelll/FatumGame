// FlecsArtillerySubsystem: Owns the simulation thread (physics + ECS).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SkeletonTypes.h"
#include "KeyedConcept.h"
#include "GameplayTagContainer.h"
#include "flecs.h"
#include "FSimulationWorker.h"
#include "FLateSyncBridge.h"
#include "FSimStateCache.h"
#include <atomic>
#include "FlecsCharacterTypes.h"
#include "FlecsRecoilTypes.h"  // FShotFiredEvent — full type needed for TQueue
#include "FlecsArtillerySubsystem.generated.h"

class UBarrageDispatch;
class UFlecsRenderManager;
class UFlecsNiagaraManager;
class UFlecsEntityDefinition;
class UFlecsItemDefinition;
class UNiagaraSystem;
struct BarrageContactEvent;
struct FItemStaticData;
class FDebrisPool;
class AFlecsCharacter;
class FBarragePrimitive;
class FBCharacterBase;
struct FRopeVisualAtomics;
struct FNoiseEvent;

/** Lightweight bridge for character physics (sim-thread-only after registration).
 *  PrepareCharacterStep reads InputAtomics, writes StateAtomics.
 *  Position readback is in AFlecsCharacter::Tick (game thread, direct Jolt read). */
struct FCharacterPhysBridge
{
	// ── Shared (immutable after registration) ──
	TSharedPtr<FBarragePrimitive> CachedBody;
	FSkeletonKey CharacterKey;

	// ── Sim thread (PrepareCharacterStep) ──
	TSharedPtr<FCharacterInputAtomics, ESPMode::ThreadSafe> InputAtomics;  // game→sim
	TSharedPtr<FCharacterStateAtomics, ESPMode::ThreadSafe> StateAtomics;  // sim→game
	flecs::entity Entity;
	TSharedPtr<FBCharacterBase> CachedFBChar;

	// ── Time dilation: base gravity for player compensation (Jolt Y, lazy-captured) ──
	float BaseGravityJoltY = 0.f;
	bool bBaseGravityCaptured = false;

	// ── Rope visual bridge (sim→game, owned by AFlecsCharacter) ──
	TSharedPtr<FRopeVisualAtomics> RopeVisualAtomics;
};

/**
 * Pending ISM render instance for a sim-thread-spawned entity.
 * Physics body + Flecs entity are created on sim thread.
 * Game thread recomputes ISM position from fresh camera + raw intent data (inverted flow).
 */
struct FPendingProjectileSpawn
{
	UStaticMesh* Mesh = nullptr;
	UMaterialInterface* Material = nullptr;
	FVector Scale = FVector::OneVector;
	FRotator RotationOffset = FRotator::ZeroRotator;
	FSkeletonKey EntityKey;

	/** Actual projectile flight direction (barrel→target, corrected for barrel offset via raycast).
	 *  Used for ISM rotation. */
	FVector SpawnDirection = FVector::ForwardVector;

	/** Fallback position if bridge is unavailable (computed by sim thread from stale aim data). */
	FVector SimComputedLocation = FVector::ZeroVector;

	/** Attached Niagara VFX to register (null = no VFX). */
	UNiagaraSystem* NiagaraEffect = nullptr;
	float NiagaraScale = 1.0f;
	FVector NiagaraOffset = FVector::ZeroVector;

	/** Death Niagara VFX (set on Flecs entity as FNiagaraDeathEffect). */
	UNiagaraSystem* DeathEffect = nullptr;
	float DeathEffectScale = 1.0f;
};

/**
 * Pending ISM render instance for a fragment from a destroyed object.
 * Fragment bodies are acquired from FDebrisPool on sim thread.
 * Game thread creates ISM instances from this queue.
 */
struct FPendingFragmentSpawn
{
	FSkeletonKey EntityKey;
	UStaticMesh* Mesh = nullptr;
	UMaterialInterface* Material = nullptr;
	FTransform WorldTransform;
};

/**
 * Owns the simulation thread that drives Barrage physics + Flecs ECS.
 *
 * Architecture:
 * - Game thread: Tick() processes cosmetics, pending spawns, EnqueueCommand()
 * - Simulation thread (FSimulationWorker): DrainCommandQueue → StackUp → StepWorld → BroadcastContactEvents → progress()
 * - Flecs worker threads: parallel system execution during progress()
 *
 * Lock-free bidirectional binding (Entity ↔ BarrageKey):
 *   Forward: entity.get<FBarrageBody>()->BarrageKey  [O(1) Flecs sparse set]
 *   Reverse: FBLet->GetFlecsEntity()                 [O(1) atomic in FBarragePrimitive]
 */
UCLASS()
class UFlecsArtillerySubsystem : public UTickableWorldSubsystem, public ISkeletonLord
{
	GENERATED_BODY()

public:
	static inline UFlecsArtillerySubsystem* SelfPtr = nullptr;

	/** Max number of Flecs stages (for future multi-threaded collision processing). */
	static constexpr int32 MaxStages = 8;

	// ═══════════════════════════════════════════════════════════════
	// SIMULATION THREAD API (called by FSimulationWorker)
	// ═══════════════════════════════════════════════════════════════

	/** Drain MPSC command queue. Called by SimWorker before Flecs progress(). */
	void DrainCommandQueue();

	/** Progress the Flecs world by DeltaTime seconds. Called by SimWorker. */
	void ProgressWorld(float DeltaTime);

	/** Ensure the calling thread has Barrage physics access. Call from Flecs worker systems that touch physics. */
	void EnsureBarrageAccess();

	/** Apply all late-sync buffers to Flecs entities. Called by sim thread before ProgressWorld(). */
	void ApplyLateSyncBuffers();

	/** Compute acceleration-smoothed locomotion for all characters. Called by FSimulationWorker before StackUp.
	 *  Reads InputAtomics (direction), FCharacterMoveState (sprint/posture), FMovementStatic (speeds/accel),
	 *  CharacterVirtual (ground state, velocity). Writes mLocomotionUpdate on FBCharacter.
	 *  @param RealDT     Wall-clock delta (undilated)
	 *  @param DilatedDT  RealDT * TimeScale (for physics)
	 *  @param TimeScale  Active time dilation [0.02, 1.0]
	 *  @param bPlayerFullSpeed  Player exempted from dilation? */
	void PrepareCharacterStep(float RealDT, float DilatedDT, float TimeScale, bool bPlayerFullSpeed);

	// ═══════════════════════════════════════════════════════════════
	// LOCAL PLAYER REGISTRATION (local player cache)
	// ═══════════════════════════════════════════════════════════════

	static inline TWeakObjectPtr<AActor> CachedLocalPlayerActor;
	static inline FSkeletonKey CachedLocalPlayerKey;

	static void RegisterLocalPlayer(AActor* Player, FSkeletonKey Key);
	static void UnregisterLocalPlayer();

	// ═══════════════════════════════════════════════════════════════
	// CHARACTER PHYSICS BRIDGE (sim thread only)
	// ═══════════════════════════════════════════════════════════════

	/** Register character for sim-thread position readback. Called from BeginPlay EnqueueCommand. */
	void RegisterCharacterBridge(AFlecsCharacter* Character);

	/** Unregister character bridge by key. Called from EndPlay EnqueueCommand. */
	void UnregisterCharacterBridge(FSkeletonKey CharacterKey);

	// ═══════════════════════════════════════════════════════════════
	// INTERPOLATION (game thread — computed in Tick, consumed by character + render manager)
	// ═══════════════════════════════════════════════════════════════

	/** Current interpolation Alpha ∈ [0,1]. 0 = just after sim tick, 1 = just before next.
	 *  Cached — computed once per Subsystem Tick (runs AFTER actor ticks). */
	float GetCurrentAlpha() const { return CachedAlpha; }

	/** Current sim tick count — cached for consistency with CachedAlpha. */
	uint64 GetCurrentSimTick() const { return CachedSimTick; }

	// ═══════════════════════════════════════════════════════════════
	// THREAD-SAFE API (callable from any thread)
	// ═══════════════════════════════════════════════════════════════

	/** Queue a command to execute on the simulation thread next tick. MPSC-safe. */
	void EnqueueCommand(TFunction<void()>&& Command);

	/** Lock-free bridge for latest-value-wins data (aim, etc). Game thread writes, sim thread reads. */
	FLateSyncBridge* GetLateSyncBridge() const { return LateSyncBridge.Get(); }

	/** Lock-free sim→game state cache for scalar ECS values (health, weapon). */
	FSimStateCache& GetSimStateCache() { return SimStateCache; }
	const FSimStateCache& GetSimStateCache() const { return SimStateCache; }

	// ═══════════════════════════════════════════════════════════════
	// BIDIRECTIONAL BINDING API (simulation thread only)
	// Lock-free O(1) lookups in both directions.
	// ═══════════════════════════════════════════════════════════════

	/** Get the Flecs world. Only safe from simulation thread. */
	flecs::world* GetFlecsWorld() const { return FlecsWorld.Get(); }

	/** Get the cached Barrage dispatch (for render manager access). */
	UBarrageDispatch* GetBarrageDispatch() const { return CachedBarrageDispatch; }

	/** Get the render manager (convenience). */
	class UFlecsRenderManager* GetRenderManager() const;

	/** Get the Niagara VFX manager (convenience). */
	class UFlecsNiagaraManager* GetNiagaraManager() const;

	/**
	 * Get a Flecs stage for thread-safe deferred commands.
	 * @param ThreadIndex Thread index (0 = main simulation thread). Use 0 for now.
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
	// DAMAGE SYSTEM API
	// Queue damage from any source - processed by DamageObserver.
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Queue damage to an entity. Processed by DamageObserver on next tick.
	 * Can be called from any damage source: collision, ability, environment, API.
	 *
	 * @param Target Target Flecs entity (must have FHealthInstance).
	 * @param Damage Base damage amount (before armor/modifiers).
	 * @param SourceEntityId Source entity (0 = environment/no source).
	 * @param DamageType Damage type tag for resistances (optional).
	 * @param HitLocation World location for effects/knockback (optional).
	 * @param bIgnoreArmor Bypass armor calculation.
	 * @return true if damage was queued successfully.
	 */
	bool QueueDamage(flecs::entity Target, float Damage, uint64 SourceEntityId = 0,
	                 FGameplayTag DamageType = FGameplayTag(), FVector HitLocation = FVector::ZeroVector,
	                 bool bIgnoreArmor = false);

	/**
	 * Queue damage to an entity by BarrageKey. Convenience wrapper.
	 * @param TargetKey BarrageKey of target entity.
	 * @param Damage Base damage amount.
	 * @param SourceEntityId Source entity (0 = environment).
	 * @param DamageType Damage type tag (optional).
	 * @return true if damage was queued successfully.
	 */
	bool QueueDamageByKey(FSkeletonKey TargetKey, float Damage, uint64 SourceEntityId = 0,
	                      FGameplayTag DamageType = FGameplayTag());

	// ═══════════════════════════════════════════════════════════════
	// ENTITY PREFAB REGISTRY
	// Prefabs store shared static data for entity types.
	// Each UFlecsEntityDefinition gets one prefab with:
	// - FHealthStatic, FDamageStatic, FProjectileStatic, etc.
	// - FEntityDefinitionRef for back-reference
	// Entity instances use is_a(Prefab) to inherit static data.
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Get or create a prefab for an entity type.
	 * Creates prefab with all Static components from the definition's profiles.
	 * Thread-safe: can be called from EnqueueCommand lambdas.
	 *
	 * @param EntityDefinition The entity definition.
	 * @return Flecs prefab entity with static components, or invalid if null.
	 */
	flecs::entity GetOrCreateEntityPrefab(class UFlecsEntityDefinition* EntityDefinition);

	/**
	 * Get or create a prefab for an item type (specialized for items).
	 * Thread-safe: can be called from EnqueueCommand lambdas.
	 *
	 * @param EntityDefinition The entity definition containing ItemDefinition.
	 * @return Flecs prefab entity with FItemStaticData, or invalid entity if no ItemDefinition.
	 */
	flecs::entity GetOrCreateItemPrefab(class UFlecsEntityDefinition* EntityDefinition);

	/**
	 * Get existing prefab by TypeId. Returns invalid entity if not registered.
	 * @param TypeId Item type ID from UFlecsItemDefinition.
	 * @return Prefab entity or invalid.
	 */
	flecs::entity GetItemPrefab(int32 TypeId) const;

	// ═══════════════════════════════════════════════════════════════
	// WEAPON SYSTEM API
	// Pending projectile spawns from WeaponFireSystem.
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Process pending projectile ISM render instances from weapon fire.
	 * Must be called on Game thread (UE rendering requires it).
	 * Physics bodies + Flecs entities are created on sim thread in WeaponFireSystem.
	 */
	void ProcessPendingProjectileSpawns();

	/**
	 * Get pending projectile spawn queue.
	 * For internal use by weapon systems.
	 */
	TQueue<FPendingProjectileSpawn, EQueueMode::Mpsc>& GetPendingProjectileSpawns() { return PendingProjectileSpawns; }

	/** Get shot-fired event queue for game-thread recoil processing. */
	TQueue<FShotFiredEvent, EQueueMode::Mpsc>& GetPendingShotEvents() { return PendingShotEvents; }

	// ═══════════════════════════════════════════════════════════════
	// DESTRUCTIBLE SYSTEM API
	// Fragment spawns from FragmentationSystem, debris pool.
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Process pending fragment ISM render instances from destructible fragmentation.
	 * Must be called on Game thread (UE rendering requires it).
	 * Called after ProcessPendingProjectileSpawns in Tick().
	 */
	void ProcessPendingFragmentSpawns();

	/**
	 * Get pending fragment spawn queue.
	 * For internal use by FragmentationSystem.
	 */
	TQueue<FPendingFragmentSpawn, EQueueMode::Mpsc>& GetPendingFragmentSpawns() { return PendingFragmentSpawns; }

	/** Get the debris pool for fragment body reuse. */
	FDebrisPool* GetDebrisPool() const { return DebrisPool; }

	/**
	 * Get EntityDefinition from an item entity (via its prefab).
	 * @param ItemEntity Item entity with is_a relationship to prefab.
	 * @return EntityDefinition pointer or nullptr.
	 */
	class UFlecsEntityDefinition* GetEntityDefinitionForItem(flecs::entity ItemEntity) const;

	/**
	 * Get ItemDefinition from an item entity (via its prefab).
	 * @param ItemEntity Item entity with is_a relationship to prefab.
	 * @return ItemDefinition pointer or nullptr.
	 */
	class UFlecsItemDefinition* GetItemDefinitionForItem(flecs::entity ItemEntity) const;

	// ═══════════════════════════════════════════════════════════════
	// DEPRECATED API (for backward compatibility during migration)
	// Will be removed after FlecsCharacter is updated.
	// ═══════════════════════════════════════════════════════════════

	UE_DEPRECATED(5.7, "Use BindEntityToBarrage instead")
	void RegisterBarrageEntity(FSkeletonKey BarrageKey, uint64 FlecsEntityId);

	UE_DEPRECATED(5.7, "Use UnbindEntityFromBarrage instead")
	void UnregisterBarrageEntity(FSkeletonKey BarrageKey);

	UE_DEPRECATED(5.7, "Use GetEntityForBarrageKey instead")
	uint64 GetFlecsEntityForBarrageKey(FSkeletonKey BarrageKey) const;

	UE_DEPRECATED(5.7, "Use HasEntityForBarrageKey instead")
	bool HasFlecsEntityForBarrageKey(FSkeletonKey BarrageKey) const;

	// ═══════════════════════════════════════════════════════════════
	// STEALTH SYSTEM API
	// ═══════════════════════════════════════════════════════════════

	/** Set the ambient light level for stealth. Thread-safe (enqueues to sim thread). */
	void SetAmbientLightLevel(float Level);

	/** Queue a noise event on a character entity. Call from game thread. */
	void QueueNoiseEvent(FSkeletonKey CharacterKey, const FNoiseEvent& Event);

	// ═══════════════════════════════════════════════════════════════
	// VITALS SYSTEM API
	// ═══════════════════════════════════════════════════════════════

	/** Set default ambient temperature for vitals warmth system (0-1). Thread-safe (enqueues to sim thread). */
	void SetDefaultAmbientTemperature(float Temperature);

	/** Default ambient temperature for vitals warmth system (0-1). Sim thread only. */
	float DefaultAmbientTemperature = 0.5f;

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

	// UTickableWorldSubsystem - Game thread tick
	virtual void Tick(float DeltaTime) override;

	/** Compute fresh interpolation Alpha from SimWorker timing atomics.
	 *  Unlike GetCurrentAlpha() (cached, stale for actors), this reads wall-clock time for a fresh value.
	 *  Used by AFlecsCharacter::Tick() which runs before subsystem Tick. */
	float ComputeFreshAlpha(uint64& OutSimTick) const;

	/** Access SimWorker for time dilation atomics (game thread writes, sim thread reads). */
	FSimulationWorker& GetSimWorker() { return SimWorker; }

	/** Get caliber registry (loaded from Project Settings → Fatum Game). */
	class UFlecsCaliberRegistry* GetCaliberRegistry() const { return CaliberRegistry; }

private:
	/** Set up Flecs systems that run on the simulation thread. */
	void SetupFlecsSystems();

	/** Register all Flecs components (called first by SetupFlecsSystems). */
	void RegisterFlecsComponents();

	/** Collision pair pipeline: Damage, Bounce, Pickup, Destructible. */
	void SetupCollisionSystems();
	void SetupDamageCollisionSystems();
	void SetupPickupCollisionSystems();
	void SetupDestructibleCollisionSystems();

	/** Constraint break detection + fragmentation spawning. */
	void SetupFragmentationSystems();

	/** Weapon tick, reload, and fire systems. */
	void SetupWeaponSystems();

	/** Door trigger unlock + door state machine. */
	void SetupDoorSystems();

	/** Stealth light + noise computation for characters. */
	void SetupStealthSystems();

	/** Vitals systems: equipment cache, drain, modifier recalc, HP drain. */
	void SetupVitalsSystems();

	/** Ambient light level for stealth (sim thread only, written via EnqueueCommand). */
	float AmbientLightLevel = 0.1f;

	/** Subscribe to Barrage collision events. */
	void SubscribeToBarrageEvents();

	/** Handle collision event from Barrage. Called on simulation thread. */
	void OnBarrageContact(const BarrageContactEvent& Event);

	/** Direct flecs::world - no UFlecsWorld wrapper, no plugin tick functions, no worker threads. */
	TUniquePtr<flecs::world> FlecsWorld;

	/** MPSC command queue: game thread producers -> simulation thread consumer. */
	TQueue<TFunction<void()>, EQueueMode::Mpsc> CommandQueue;

	/** Delegate handle for Barrage contact events (for cleanup). */
	FDelegateHandle ContactEventHandle;

	// ═══════════════════════════════════════════════════════════════
	// INTERPOLATION STATE (game thread only, computed in Tick)
	// ═══════════════════════════════════════════════════════════════
	float CachedAlpha = 1.f;
	uint64 CachedSimTick = 0;

	// ═══════════════════════════════════════════════════════════════
	// CACHED SUBSYSTEM POINTERS
	// ═══════════════════════════════════════════════════════════════
	UBarrageDispatch* CachedBarrageDispatch = nullptr;

	// ═══════════════════════════════════════════════════════════════
	// CALIBER REGISTRY (loaded from Project Settings → Fatum Game)
	// ═══════════════════════════════════════════════════════════════
	class UFlecsCaliberRegistry* CaliberRegistry = nullptr;

	// ═══════════════════════════════════════════════════════════════
	// SIMULATION THREAD (simulation thread)
	// ═══════════════════════════════════════════════════════════════
	FSimulationWorker SimWorker;
	FRunnableThread* SimThread = nullptr;

	void StartSimulationThread();
	void StopSimulationThread();

	// ═══════════════════════════════════════════════════════════════
	// PREFAB STORAGE
	// Maps definition pointers → Flecs prefab entities.
	// Only accessed from simulation thread.
	// ═══════════════════════════════════════════════════════════════

	/** EntityDefinition* → Prefab entity. General entity prefabs. */
	TMap<UFlecsEntityDefinition*, flecs::entity> EntityPrefabs;

	/** TypeId → Prefab entity. Item-specific prefabs (for lookup by TypeId). */
	TMap<int32, flecs::entity> ItemPrefabs;

	// ═══════════════════════════════════════════════════════════════
	// WEAPON SYSTEM
	// Pending projectile spawns queued by WeaponFireSystem.
	// ═══════════════════════════════════════════════════════════════

	/** Queue of projectile spawns from weapon fire. Sim → Game thread. */
	TQueue<FPendingProjectileSpawn, EQueueMode::Mpsc> PendingProjectileSpawns;

	/** Queue of shot-fired events for game-thread recoil feedback. Sim → Game thread.
	 *  One event per trigger pull (not per pellet). Drained by AFlecsCharacter::Tick(). */
	TQueue<FShotFiredEvent, EQueueMode::Mpsc> PendingShotEvents;

	// ═══════════════════════════════════════════════════════════════
	// DESTRUCTIBLE SYSTEM
	// Fragment spawns + debris body pool.
	// ═══════════════════════════════════════════════════════════════

	/** Queue of fragment ISM spawns from FragmentationSystem. Sim → Game thread. */
	TQueue<FPendingFragmentSpawn, EQueueMode::Mpsc> PendingFragmentSpawns;

	/** Reusable Barrage body pool for debris fragments. */
	FDebrisPool* DebrisPool = nullptr;

	/** Bridge for lock-free game→sim "latest wins" data. */
	TUniquePtr<FLateSyncBridge> LateSyncBridge;

	/** Lock-free sim→game state cache (health, weapon snapshots). */
	FSimStateCache SimStateCache;

	// ═══════════════════════════════════════════════════════════════
	// CHARACTER PHYSICS BRIDGE (sim thread only)
	// PrepareCharacterStep reads InputAtomics + writes mLocomotionUpdate + SlideActive.
	// Registration/unregistration via EnqueueCommand (sim thread).
	// Position readback moved to AFlecsCharacter::Tick (game thread, direct Jolt read).
	// No lock needed — all access is sequential on sim thread.
	// ═══════════════════════════════════════════════════════════════
	TArray<FCharacterPhysBridge> CharacterBridges;
};
