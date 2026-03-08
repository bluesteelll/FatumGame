// FlecsArtillerySubsystem - Flecs Systems Setup (Orchestrator)
// Contains: RegisterFlecsComponents, DamageObserver, lifecycle systems, cleanup systems.
// Domain systems are in separate files:
//   _CollisionSystems.cpp, _FragmentationSystems.cpp, _WeaponSystems.cpp, _DoorSystems.cpp

#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FlecsGameTags.h"
#include "FlecsBarrageComponents.h"
#include "FlecsHealthComponents.h"
#include "FlecsProjectileComponents.h"
#include "FlecsEntityComponents.h"
#include "FlecsItemComponents.h"
#include "FlecsDestructibleComponents.h"
#include "FlecsInteractionComponents.h"
#include "FlecsWeaponComponents.h"
#include "EPhysicsLayer.h"
#include "BarrageSpawnUtils.h"
#include "FlecsRenderManager.h"
#include "FlecsEntityDefinition.h"
#include "FlecsPhysicsProfile.h"
#include "FlecsProjectileProfile.h"
#include "FlecsRenderProfile.h"
#include "FlecsDamageProfile.h"
#include "FBShapeParams.h"
#include "Skeletonize.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsMovementStatic.h"
#include "FlecsUIMessages.h"
#include "FlecsNiagaraManager.h"
#include "FlecsNiagaraProfile.h"
#include "FlecsDestructibleProfile.h"
#include "FlecsDestructibleGeometry.h"
#include "FlecsHealthProfile.h"
#include "FDebrisPool.h"
#include "BarrageConstraintSystem.h"
#include "FBConstraintParams.h"
#include "PhysicsFilters/FastObjectLayerFilters.h"
#include "FlecsDoorComponents.h"
#include "FlecsMovementComponents.h"
#include "FlecsAbilityTypes.h"
#include "FlecsAbilityStates.h"
#include "FlecsResourceTypes.h"
#include "AbilityTickFunctions.h"

// ═══════════════════════════════════════════════════════════════
// COMPONENT REGISTRATION
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::RegisterFlecsComponents()
{
	flecs::world& World = *FlecsWorld;

	// ─────────────────────────────────────────────────────────
	// STATIC COMPONENTS (Prefab - shared data per entity type)
	// ─────────────────────────────────────────────────────────
	World.component<FHealthStatic>();
	World.component<FDamageStatic>();
	World.component<FProjectileStatic>();
	World.component<FLootStatic>();
	World.component<FItemStaticData>();
	World.component<FContainerStatic>();
	World.component<FEntityDefinitionRef>();
	World.component<FInteractionStatic>();
	World.component<FDestructibleStatic>();

	// ─────────────────────────────────────────────────────────
	// INSTANCE COMPONENTS (Per-entity mutable data)
	// ─────────────────────────────────────────────────────────
	World.component<FHealthInstance>();
	World.component<FProjectileInstance>();
	World.component<FItemInstance>();
	World.component<FItemUniqueData>();
	World.component<FContainerInstance>();
	World.component<FContainerGridInstance>();
	World.component<FContainerSlotsInstance>();
	World.component<FWorldItemInstance>();
	World.component<FContainedIn>();
	World.component<FDebrisInstance>();
	World.component<FFragmentationData>();
	World.component<FInteractionInstance>();
	World.component<FFocusCameraOverride>();
	World.component<FInteractionAngleOverride>();

	// Damage Event System
	World.component<FDamageHit>();
	World.component<FPendingDamage>();

	// ─────────────────────────────────────────────────────────
	// PHYSICS BRIDGE COMPONENTS
	// ─────────────────────────────────────────────────────────
	World.component<FBarrageBody>();
	World.component<FISMRender>();

	// ─────────────────────────────────────────────────────────
	// ENTITY TAGS (zero-size, for archetype queries)
	// ─────────────────────────────────────────────────────────
	World.component<FTagItem>();
	World.component<FTagDroppedItem>();
	World.component<FTagContainer>();
	World.component<FTagDestructible>();
	World.component<FTagPickupable>();
	World.component<FTagHasLoot>();
	World.component<FTagDead>();
	World.component<FTagProjectile>();
	World.component<FTagCharacter>();
	World.component<FTagEquipment>();
	World.component<FTagConsumable>();
	World.component<FTagInteractable>();
	World.component<FTagConstrained>();
	World.component<FTagDebrisFragment>();

	// ─────────────────────────────────────────────────────────
	// WEAPON COMPONENTS
	// ─────────────────────────────────────────────────────────
	World.component<FAimDirection>();
	World.component<FWeaponStatic>();
	World.component<FWeaponInstance>();
	World.component<FEquippedBy>();
	World.component<FTagWeapon>();

	// ─────────────────────────────────────────────────────────
	// MOVEMENT COMPONENTS
	// ─────────────────────────────────────────────────────────
	World.component<FMovementState>();
	World.component<FCharacterMoveState>();
	World.component<FMovementStatic>();
	World.component<FCharacterSimState>();
	World.component<FAbilitySystem>();
	World.component<FSlideState>();
	World.component<FBlinkState>();
	World.component<FMantleState>();
	World.component<FTelekinesisState>();
	World.component<FResourcePools>();
	World.component<FTagTelekinesisHeld>();

	// ─────────────────────────────────────────────────────────
	// COLLISION PAIR SYSTEM COMPONENTS
	// ─────────────────────────────────────────────────────────
	World.component<FCollisionPair>();
	World.component<FTagCollisionDamage>();
	World.component<FTagCollisionPickup>();
	World.component<FTagCollisionBounce>();
	World.component<FTagCollisionDestructible>();
	World.component<FTagCollisionCharacter>();
	World.component<FTagCollisionFragmentation>();
	World.component<FTagCollisionProcessed>();

	// ─────────────────────────────────────────────────────────
	// VFX COMPONENTS
	// ─────────────────────────────────────────────────────────
	World.component<FNiagaraDeathEffect>();
	World.component<FDeathContactPoint>();

	// ─────────────────────────────────────────────────────────
	// CONSTRAINT COMPONENTS
	// ─────────────────────────────────────────────────────────
	World.component<FConstraintLink>();
	World.component<FFlecsConstraintData>();

	// ─────────────────────────────────────────────────────────
	// DOOR COMPONENTS
	// ─────────────────────────────────────────────────────────
	World.component<FDoorStatic>();
	World.component<FDoorInstance>();
	World.component<FDoorTriggerLink>();
	World.component<FTagDoor>();
	World.component<FTagDoorTrigger>();
}

// ═══════════════════════════════════════════════════════════════
// DEAD ENTITY CLEANUP HELPERS (file-local)
// Used by DeadEntityCleanupSystem to keep the system lambda compact.
// ═══════════════════════════════════════════════════════════════

static void CleanupConstraints(flecs::entity Entity, UBarrageDispatch* Barrage)
{
	if (!Entity.has<FTagConstrained>() || !Barrage) return;

	const FBarrageBody* ConstrainedBody = Entity.try_get<FBarrageBody>();
	if (!ConstrainedBody || !ConstrainedBody->IsValid()) return;

	FBLet ConstrainedPrim = Barrage->GetShapeRef(ConstrainedBody->BarrageKey);
	if (!FBarragePrimitive::IsNotNull(ConstrainedPrim)) return;

	FBarrageConstraintSystem* ConstraintSys = Barrage->GetConstraintSystem();
	if (ConstraintSys)
	{
		ConstraintSys->RemoveAllForBody(ConstrainedPrim->KeyIntoBarrage);
	}
}

/** Removes ISM render instance and queues death VFX + Niagara removal.
 *  Returns false if the entity was destructed (no physics prim for VFX fallback). */
static bool CleanupRenderAndVFX(flecs::entity Entity, FSkeletonKey Key, UBarrageDispatch* Barrage)
{
	UWorld* World = Barrage ? Barrage->GetWorld() : nullptr;

	// Remove ISM render instance
	if (World)
	{
		if (UFlecsRenderManager* Renderer = UFlecsRenderManager::Get(World))
		{
			Renderer->RemoveInstance(Key);
		}
	}

	// Queue death VFX + unregister from Niagara
	UFlecsNiagaraManager* NiagaraMgr = UFlecsNiagaraManager::Get(World);
	if (!NiagaraMgr) return true;

	const FNiagaraDeathEffect* DeathVFX = Entity.try_get<FNiagaraDeathEffect>();
	if (DeathVFX && DeathVFX->Effect && Barrage)
	{
		FPendingDeathEffect FX;
		FX.Effect = DeathVFX->Effect;
		FX.Scale = DeathVFX->Scale;

		const FDeathContactPoint* DCP = Entity.try_get<FDeathContactPoint>();
		if (DCP && !DCP->Position.IsNearlyZero())
		{
			FX.Location = DCP->Position;
		}
		else
		{
			FBLet DeathPrim = Barrage->GetShapeRef(Key);
			if (FBarragePrimitive::IsNotNull(DeathPrim))
			{
				FX.Location = FVector(FBarragePrimitive::GetPosition(DeathPrim));
				FVector DeathVel(FBarragePrimitive::GetVelocity(DeathPrim));
				if (!DeathVel.IsNearlyZero())
				{
					FX.Rotation = FRotationMatrix::MakeFromX(DeathVel).ToQuat();
				}
			}
			else
			{
				// No physics prim for VFX fallback — just unregister and destruct
				NiagaraMgr->EnqueueRemoval(Key);
				Entity.destruct();
				return false;
			}
		}
		NiagaraMgr->EnqueueDeathEffect(FX);
	}
	NiagaraMgr->EnqueueRemoval(Key);
	return true;
}

/** Releases pooled debris back to FDebrisPool. Returns true if handled (entity destructed). */
static bool TryReleaseToPool(flecs::entity Entity, FSkeletonKey Key, UBarrageDispatch* Barrage, FDebrisPool* Pool)
{
	const FDebrisInstance* Debris = Entity.try_get<FDebrisInstance>();
	if (!Debris || Debris->PoolSlotIndex == INDEX_NONE) return false;
	if (!Pool || !Pool->IsInitialized()) return false;

	FBLet Prim = Barrage->GetShapeRef(Key);
	if (FBarragePrimitive::IsNotNull(Prim))
	{
		Prim->ClearFlecsEntity();
	}
	Pool->Release(Key);
	Entity.destruct();
	return true;
}

/** Normal destruction path: clear reverse binding, move to DEBRIS layer, tombstone. */
static void TombstoneBody(flecs::entity Entity, FSkeletonKey Key, UBarrageDispatch* Barrage)
{
	if (!Barrage) return;

	FBLet Prim = Barrage->GetShapeRef(Key);
	if (FBarragePrimitive::IsNotNull(Prim))
	{
		Prim->ClearFlecsEntity();
		FBarrageKey BarrageKey = Prim->KeyIntoBarrage;
		Barrage->SetBodyObjectLayer(BarrageKey, Layers::DEBRIS);
		Barrage->SuggestTombstone(Prim);
	}
}

// ═══════════════════════════════════════════════════════════════
// SYSTEM ORCHESTRATOR
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::SetupFlecsSystems()
{
	flecs::world& World = *FlecsWorld;

	// ─────────────────────────────────────────────────────────
	// 1. COMPONENT REGISTRATION (must be first)
	// ─────────────────────────────────────────────────────────
	RegisterFlecsComponents();
	InitAbilityTickFunctions();

	// ═══════════════════════════════════════════════════════════════
	// OBSERVERS (Event-driven processing)
	// ═══════════════════════════════════════════════════════════════

	// ─────────────────────────────────────────────────────────
	// DAMAGE OBSERVER
	// Processes FPendingDamage when it's set/modified on any entity.
	// This decouples damage sources from damage application:
	// - Projectile collision → queues damage
	// - Abilities/Spells → queue damage
	// - Environment (fire) → queue damage
	// - API calls → queue damage
	// All processed uniformly here.
	// ─────────────────────────────────────────────────────────
	World.observer<FPendingDamage, FHealthInstance>("DamageObserver")
		.event(flecs::OnSet)
		.each([this](flecs::entity Entity, FPendingDamage& Pending, FHealthInstance& Health)
		{
			if (!Pending.HasPendingDamage())
			{
				return;
			}

			// Get static health data (armor) from prefab via IsA
			const FHealthStatic* HealthStatic = Entity.try_get<FHealthStatic>();
			const float BaseArmor = HealthStatic ? HealthStatic->Armor : 0.f;
			const float MaxHP = HealthStatic ? HealthStatic->MaxHP : 100.f;

			float TotalDamageApplied = 0.f;

			// Process each hit individually (armor applies per-hit)
			for (const FDamageHit& Hit : Pending.Hits)
			{
				if (Entity.has<FTagDead>())
				{
					break; // Already dead, skip remaining hits
				}

				// Calculate effective armor (can be bypassed)
				float EffectiveArmor = Hit.bIgnoreArmor ? 0.f : BaseArmor;

				// TODO: Apply damage type resistances/weaknesses here
				// if (Hit.DamageType.IsValid()) { ... }

				// Calculate final damage
				float FinalDamage = FMath::Max(0.f, Hit.Damage - EffectiveArmor);

				// Apply critical multiplier
				if (Hit.bIsCritical)
				{
					// TODO: Get crit multiplier from source or use default
					FinalDamage *= 2.0f;
				}

				// Apply damage
				Health.CurrentHP -= FinalDamage;
				TotalDamageApplied += FinalDamage;

				UE_LOG(LogTemp, Log, TEXT("DAMAGE: Entity %llu took %.1f damage (%.1f base, %.1f armor) HP: %.1f/%.1f"),
					Entity.id(), FinalDamage, Hit.Damage, EffectiveArmor, Health.CurrentHP, MaxHP);
			}

			// Clear processed hits (keeps array capacity for reuse)
			Pending.Clear();

			// Broadcast health change to message system (sim→game thread)
			if (TotalDamageApplied > 0.f)
			{
				if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
				{
					FUIHealthMessage HealthMsg;
					HealthMsg.EntityId = static_cast<int64>(Entity.id());
					HealthMsg.CurrentHP = Health.CurrentHP;
					HealthMsg.MaxHP = MaxHP;
					MsgSub->EnqueueMessage(TAG_UI_Health, HealthMsg);
				}

				// Update sim→game state cache
				SimStateCache.WriteHealth(static_cast<int64>(Entity.id()), Health.CurrentHP, MaxHP);
			}
		});

	// ═══════════════════════════════════════════════════════════════
	// GAMEPLAY SYSTEMS (lifecycle)
	// ═══════════════════════════════════════════════════════════════

	// ─────────────────────────────────────────────────────────
	// WORLD ITEM DESPAWN SYSTEM
	// World items with FWorldItemInstance get their DespawnTimer
	// decremented. When it hits 0, the entity is tagged FTagDead.
	// Uses FWorldItemInstance for despawn/grace timers.
	// ─────────────────────────────────────────────────────────
	World.system<FWorldItemInstance>("WorldItemDespawnSystem")
		.with<FTagItem>()
		.without<FTagDead>()
		.each([](flecs::entity Entity, FWorldItemInstance& WorldItem)
		{
			if (WorldItem.DespawnTimer > 0.f)
			{
				const float DeltaTime = Entity.world().get_info()->delta_time;
				WorldItem.DespawnTimer -= DeltaTime;
				if (WorldItem.DespawnTimer <= 0.f)
				{
					Entity.add<FTagDead>();
				}
			}
		});

	// ─────────────────────────────────────────────────────────
	// PICKUP GRACE SYSTEM
	// World items with FTagDroppedItem get their PickupGraceTimer
	// decremented. When it hits 0, the tag is removed.
	// Uses FWorldItemInstance for despawn/grace timers.
	// ─────────────────────────────────────────────────────────
	World.system<FWorldItemInstance>("PickupGraceSystem")
		.with<FTagDroppedItem>()
		.each([](flecs::entity Entity, FWorldItemInstance& WorldItem)
		{
			if (WorldItem.PickupGraceTimer > 0.f)
			{
				const float DeltaTime = Entity.world().get_info()->delta_time;
				WorldItem.PickupGraceTimer -= DeltaTime;
				if (WorldItem.PickupGraceTimer <= 0.f)
				{
					Entity.remove<FTagDroppedItem>();
				}
			}
			else
			{
				Entity.remove<FTagDroppedItem>();
			}
		});

	// ─────────────────────────────────────────────────────────
	// PROJECTILE LIFETIME SYSTEM
	// Uses NEW Static/Instance architecture:
	// - FProjectileStatic (prefab): MaxBounces, MinVelocity, GracePeriodFrames
	// - FProjectileInstance (entity): LifetimeRemaining, BounceCount, GraceFramesRemaining
	// Velocity check is DISABLED for true bouncing projectiles (MaxBounces == -1).
	// ─────────────────────────────────────────────────────────
	World.system<FProjectileInstance, const FBarrageBody>("ProjectileLifetimeSystem")
		.with<FTagProjectile>()
		.without<FTagDead>()
		.each([this](flecs::entity Entity, FProjectileInstance& ProjInstance, const FBarrageBody& Body)
		{
			EnsureBarrageAccess();

			// Get static data from prefab (via IsA inheritance)
			const FProjectileStatic* ProjStatic = Entity.try_get<FProjectileStatic>();

			const float DeltaTime = Entity.world().get_info()->delta_time;
			ProjInstance.LifetimeRemaining -= DeltaTime;
			if (ProjInstance.LifetimeRemaining <= 0.f)
			{
				Entity.add<FTagDead>();
				return;
			}

			// Get MaxBounces from static, default to -1 (infinite) if no prefab
			const int32 MaxBounces = ProjStatic ? ProjStatic->MaxBounces : -1;

			// TRUE bouncing projectiles never die by velocity check
			if (MaxBounces == -1)
			{
				return;
			}

			// Decrement grace period counter
			if (ProjInstance.GraceFramesRemaining > 0)
			{
				ProjInstance.GraceFramesRemaining--;
				return;
			}

			// Kill stopped projectiles - MinVelocity from static, default 50
			const float MinVelocity = ProjStatic ? ProjStatic->MinVelocity : 50.f;
			const float MinVelocitySq = MinVelocity * MinVelocity;

			if (Body.IsValid() && CachedBarrageDispatch)
			{
				FBLet Prim = CachedBarrageDispatch->GetShapeRef(Body.BarrageKey);
				if (FBarragePrimitive::IsNotNull(Prim))
				{
					FVector3f Velocity = FBarragePrimitive::GetVelocity(Prim);
					if (Velocity.SizeSquared() < MinVelocitySq)
					{
						Entity.add<FTagDead>();
					}
				}
				else
				{
					Entity.add<FTagDead>();
				}
			}
		});

	// ─────────────────────────────────────────────────────────
	// DEBRIS LIFETIME SYSTEM
	// Counts down FDebrisInstance.LifetimeRemaining for auto-destroy fragments.
	// Tags expired fragments with FTagDead.
	// ─────────────────────────────────────────────────────────
	World.system<FDebrisInstance>("DebrisLifetimeSystem")
		.with<FTagDebrisFragment>()
		.without<FTagDead>()
		.each([](flecs::entity Entity, FDebrisInstance& Debris)
		{
			if (!Debris.bAutoDestroy) return;

			const float DeltaTime = Entity.world().get_info()->delta_time;
			Debris.LifetimeRemaining -= DeltaTime;
			if (Debris.LifetimeRemaining <= 0.f)
			{
				Entity.add<FTagDead>();
			}
		});

	// ═══════════════════════════════════════════════════════════════
	// DOMAIN SYSTEMS (separate files)
	// Declaration order = execution order within OnUpdate phase.
	// ═══════════════════════════════════════════════════════════════
	SetupCollisionSystems();     // Damage → Bounce → Pickup → Destructible
	SetupFragmentationSystems(); // ConstraintBreak, Fragmentation
	SetupWeaponSystems();        // WeaponTick, WeaponReload, WeaponFire
	SetupDoorSystems();          // TriggerUnlock, DoorTick

	// ═══════════════════════════════════════════════════════════════
	// CLEANUP SYSTEMS
	// ═══════════════════════════════════════════════════════════════

	// ─────────────────────────────────────────────────────────
	// DEATH CHECK SYSTEM
	// Entities with FHealthInstance that have CurrentHP <= 0.
	// ─────────────────────────────────────────────────────────
	World.system<const FHealthInstance>("DeathCheckSystem")
		.without<FTagDead>()
		.each([this](flecs::entity Entity, const FHealthInstance& Health)
		{
			if (!Health.IsAlive())
			{
				Entity.add<FTagDead>();

				if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
				{
					FUIDeathMessage DeathMsg;
					DeathMsg.EntityId = static_cast<int64>(Entity.id());
					MsgSub->EnqueueMessage(TAG_UI_Death, DeathMsg);
				}

				// Update sim→game state cache (HP=0)
				const FHealthStatic* HS = Entity.try_get<FHealthStatic>();
				SimStateCache.WriteHealth(static_cast<int64>(Entity.id()), 0.f, HS ? HS->MaxHP : 100.f);
			}
		});

	// ─────────────────────────────────────────────────────────
	// DEAD ENTITY CLEANUP SYSTEM
	// Entities tagged FTagDead get fully cleaned up:
	// 1. Remove constraints (must be first — constraints reference bodies)
	// 2. Remove ISM + queue death VFX
	// 3. Release pooled debris OR tombstone physics body
	// 4. Destroy Flecs entity
	// Helpers: CleanupConstraints, CleanupRenderAndVFX, TryReleaseToPool, TombstoneBody
	// ─────────────────────────────────────────────────────────
	World.system<>("DeadEntityCleanupSystem")
		.with<FTagDead>()
		.each([this](flecs::entity Entity)
		{
			EnsureBarrageAccess();

			// Free cache slot before destruction
			SimStateCache.Unregister(static_cast<int64>(Entity.id()));

			CleanupConstraints(Entity, CachedBarrageDispatch);

			const FBarrageBody* Body = Entity.try_get<FBarrageBody>();
			if (Body && Body->IsValid())
			{
				FSkeletonKey Key = Body->BarrageKey;
				if (!CleanupRenderAndVFX(Entity, Key, CachedBarrageDispatch))
					return; // Entity already destructed (no physics prim for VFX)
				if (TryReleaseToPool(Entity, Key, CachedBarrageDispatch, GetDebrisPool()))
					return; // Returned to debris pool
				TombstoneBody(Entity, Key, CachedBarrageDispatch);
			}

			Entity.destruct();
		});

	// ─────────────────────────────────────────────────────────
	// COLLISION PAIR CLEANUP SYSTEM (runs LAST)
	// Destroys all collision pair entities at the end of each tick.
	// ─────────────────────────────────────────────────────────
	World.system<>("CollisionPairCleanupSystem")
		.with<FCollisionPair>()
		.each([](flecs::entity PairEntity)
		{
			PairEntity.destruct();
		});
}
