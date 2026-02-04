// Artillery-Flecs Bridge: Runs Flecs ECS world on the Artillery 120Hz thread.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ArtilleryActorControllerConcepts.h"
#include "SkeletonTypes.h"
#include "GameplayTagContainer.h"
#include "ORDIN.h"
#include "flecs.h"
#include "HAL/ThreadSafeBool.h"
#include <atomic>
#include "FlecsArtillerySubsystem.generated.h"

class UBarrageDispatch;
class UArtilleryDispatch;
class UBarrageRenderManager;
class UFlecsEntityDefinition;
class UFlecsItemDefinition;
struct BarrageContactEvent;
struct FItemStaticData;

/**
 * Pending projectile spawn request.
 * Queued by WeaponFireSystem (Artillery thread), processed on Game thread.
 */
struct FPendingProjectileSpawn
{
	UFlecsEntityDefinition* ProjectileDefinition = nullptr;
	FVector Location = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	float SpeedMultiplier = 1.0f;
	float DamageMultiplier = 1.0f;
	int64 OwnerEntityId = 0;
	int32 ProjectilesPerShot = 1;
};

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

	/** Get the cached Barrage dispatch (for render manager access). */
	UBarrageDispatch* GetBarrageDispatch() const { return CachedBarrageDispatch; }

	/** Get the render manager (convenience). */
	class UBarrageRenderManager* GetRenderManager() const;

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
	 * Process pending projectile spawns from weapon fire.
	 * Must be called on Game thread (e.g., from Tick or callback).
	 * Spawns actual Barrage bodies and Flecs entities.
	 */
	void ProcessPendingProjectileSpawns();

	/**
	 * Get pending projectile spawn queue.
	 * For internal use by weapon systems.
	 */
	TQueue<FPendingProjectileSpawn, EQueueMode::Mpsc>& GetPendingProjectileSpawns() { return PendingProjectileSpawns; }

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

	// UTickableWorldSubsystem - Game thread tick
	virtual void Tick(float DeltaTime) override;

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

	// ═══════════════════════════════════════════════════════════════
	// PREFAB STORAGE
	// Maps definition pointers → Flecs prefab entities.
	// Only accessed from Artillery thread.
	// ═══════════════════════════════════════════════════════════════

	/** EntityDefinition* → Prefab entity. General entity prefabs. */
	TMap<UFlecsEntityDefinition*, flecs::entity> EntityPrefabs;

	/** TypeId → Prefab entity. Item-specific prefabs (for lookup by TypeId). */
	TMap<int32, flecs::entity> ItemPrefabs;

	// ═══════════════════════════════════════════════════════════════
	// WEAPON SYSTEM
	// Pending projectile spawns queued by WeaponFireSystem.
	// ═══════════════════════════════════════════════════════════════

	/** Queue of projectile spawns from weapon fire. Artillery → Game thread. */
	TQueue<FPendingProjectileSpawn, EQueueMode::Mpsc> PendingProjectileSpawns;
};
