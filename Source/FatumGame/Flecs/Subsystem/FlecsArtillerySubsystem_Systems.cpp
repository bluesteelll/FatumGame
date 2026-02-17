// FlecsArtillerySubsystem - Flecs Systems Setup

#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FlecsGameTags.h"
#include "FlecsStaticComponents.h"
#include "FlecsInstanceComponents.h"
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

void UFlecsArtillerySubsystem::SetupFlecsSystems()
{
	flecs::world& World = *FlecsWorld;

	// ═══════════════════════════════════════════════════════════════
	// COMPONENT REGISTRATION
	// Register all Flecs components (using direct flecs API)
	// ═══════════════════════════════════════════════════════════════

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
		.each([&World](flecs::entity Entity, FPendingDamage& Pending, FHealthInstance& Health)
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
			}

			// Log total if multiple hits
			if (TotalDamageApplied > 0.f && Pending.Hits.Num() > 1)
			{
				UE_LOG(LogTemp, Log, TEXT("DAMAGE: Entity %llu total damage this tick: %.1f"),
					Entity.id(), TotalDamageApplied);
			}
		});

	// ═══════════════════════════════════════════════════════════════
	// CONSTRAINT BREAK SYSTEM (runs FIRST — before gameplay systems)
	// Calls Barrage ProcessBreakableConstraints(), removes broken constraints
	// from FFlecsConstraintData on linked entities.
	// ═══════════════════════════════════════════════════════════════

	World.system<>("ConstraintBreakSystem")
		.kind(flecs::OnUpdate)
		.run([this](flecs::iter& It)
		{
			EnsureBarrageAccess();

			FBarrageConstraintSystem* ConstraintSys = CachedBarrageDispatch
				? CachedBarrageDispatch->GetConstraintSystem()
				: nullptr;
			if (!ConstraintSys) return;

			TArray<FBarrageConstraintKey> BrokenConstraints;
			int32 BrokenCount = ConstraintSys->ProcessBreakableConstraints(&BrokenConstraints);
			if (BrokenCount == 0) return;

			int32 Remaining = ConstraintSys->GetConstraintCount();
			UE_LOG(LogTemp, Warning, TEXT("CONSTRAINT_BREAK: %d constraints BROKEN this tick (remaining: %d)"),
				BrokenCount, Remaining);

			flecs::world& World = *FlecsWorld;

			// Remove broken constraint references from Flecs entities
			// Query all entities with FFlecsConstraintData
			World.each([&BrokenConstraints](flecs::entity Entity, FFlecsConstraintData& Data)
			{
				bool bChanged = false;
				for (const FBarrageConstraintKey& BrokenKey : BrokenConstraints)
				{
					if (Data.RemoveConstraint(BrokenKey.Key))
					{
						bChanged = true;
					}
				}

				if (bChanged && !Data.HasConstraints())
				{
					Entity.remove<FTagConstrained>();
				}
			});
		});

	// ═══════════════════════════════════════════════════════════════
	// GAMEPLAY SYSTEMS
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
	// COLLISION PAIR SYSTEMS
	// Process collision pairs created by OnBarrageContact().
	// Order: Damage → Bounce → Pickup → Destructible → Fragmentation
	// ═══════════════════════════════════════════════════════════════

	// ─────────────────────────────────────────────────────────
	// DAMAGE COLLISION SYSTEM
	// Queues damage to FPendingDamage (processed by DamageObserver).
	// Uses Static/Instance architecture:
	// - FDamageStatic (prefab): Damage, DamageType, bAreaDamage, CritChance
	// - FPendingDamage (target): Queued damage hits
	// ─────────────────────────────────────────────────────────
	World.system<const FCollisionPair>("DamageCollisionSystem")
		.with<FTagCollisionDamage>()
		.without<FTagCollisionProcessed>()
		.each([&World](flecs::entity PairEntity, const FCollisionPair& Pair)
		{
			uint64 ProjectileId = Pair.GetProjectileEntityId();
			uint64 TargetId = Pair.GetTargetEntityId();

			// Default damage for projectiles without FDamageStatic
			float Damage = 25.f;
			FGameplayTag DamageType;
			bool bAreaDamage = false;
			bool bDestroyOnHit = false;
			float CritChance = 0.f;
			float CritMultiplier = 2.f;
			int32 MaxBounces = 0;

			flecs::entity ProjectileEntity;

			// Get damage data from projectile's FDamageStatic (prefab)
			if (ProjectileId != 0)
			{
				ProjectileEntity = World.entity(ProjectileId);
				if (ProjectileEntity.is_valid())
				{
					const FDamageStatic* DmgStatic = ProjectileEntity.try_get<FDamageStatic>();
					const FProjectileStatic* ProjStatic = ProjectileEntity.try_get<FProjectileStatic>();

					if (DmgStatic)
					{
						Damage = DmgStatic->Damage;
						DamageType = DmgStatic->DamageType;
						bAreaDamage = DmgStatic->bAreaDamage;
						bDestroyOnHit = DmgStatic->bDestroyOnHit;
						CritChance = DmgStatic->CritChance;
						CritMultiplier = DmgStatic->CritMultiplier;
					}

					if (ProjStatic)
					{
						MaxBounces = ProjStatic->MaxBounces;
					}
				}
			}

			// Queue damage to target via FPendingDamage
			if (TargetId != 0)
			{
				flecs::entity Target = World.entity(TargetId);
				if (Target.is_valid() && !Target.has<FTagDead>())
				{
					// Skip self-damage: don't let projectiles damage their owner
					if (ProjectileEntity.is_valid())
					{
						const FProjectileInstance* ProjInst = ProjectileEntity.try_get<FProjectileInstance>();
						if (ProjInst && ProjInst->OwnerEntityId != 0
							&& static_cast<uint64>(ProjInst->OwnerEntityId) == TargetId)
						{
							PairEntity.add<FTagCollisionProcessed>();
							return;
						}
					}

					// Check if target can receive damage
					if (Target.has<FHealthInstance>())
					{
						// Roll for critical hit
						bool bIsCritical = (CritChance > 0.f && FMath::FRand() < CritChance);

						// Get or add FPendingDamage component - obtain() adds if missing and returns reference
						FPendingDamage& Pending = Target.obtain<FPendingDamage>();

						// Queue the damage hit
						Pending.AddHit(
							Damage,
							ProjectileId,
							DamageType,
							Pair.ContactPoint,
							bIsCritical,
							false  // bIgnoreArmor
						);

						// Trigger the observer
						Target.modified<FPendingDamage>();

						UE_LOG(LogTemp, Log, TEXT("COLLISION: Queued %.1f damage to Entity %llu (Crit=%d)"),
							Damage, TargetId, bIsCritical);
					}
				}
			}

			// Kill non-bouncing, non-area projectile after hit
			if (ProjectileEntity.is_valid())
			{
				bool bIsBouncing = (MaxBounces == -1);
				if (!bAreaDamage && !bIsBouncing)
				{
					// Store contact point for accurate death VFX position
					FDeathContactPoint DCP;
					DCP.Position = Pair.ContactPoint;
					ProjectileEntity.set<FDeathContactPoint>(DCP);

					ProjectileEntity.add<FTagDead>();
					UE_LOG(LogTemp, Log, TEXT("COLLISION: Projectile %llu killed after damage hit at (%.0f,%.0f,%.0f)"),
						ProjectileId, Pair.ContactPoint.X, Pair.ContactPoint.Y, Pair.ContactPoint.Z);
				}
			}

			PairEntity.add<FTagCollisionProcessed>();
		});

	// ─────────────────────────────────────────────────────────
	// BOUNCE COLLISION SYSTEM
	// Uses Static/Instance architecture:
	// - FProjectileStatic (prefab): MaxBounces
	// - FProjectileInstance (entity): BounceCount
	// No grace period — owner check handles self-collision.
	// MaxBounces=N means "allow N bounces, die on N+1th contact".
	// ─────────────────────────────────────────────────────────
	World.system<const FCollisionPair>("BounceCollisionSystem")
		.with<FTagCollisionBounce>()
		.without<FTagCollisionProcessed>()
		.each([&World](flecs::entity PairEntity, const FCollisionPair& Pair)
		{
			FVector ContactPoint = Pair.ContactPoint;
			auto ProcessBounce = [&World, ContactPoint](uint64 EntityId, uint64 OtherId) -> bool
			{
				if (EntityId == 0) return false;

				flecs::entity Entity = World.entity(EntityId);
				if (!Entity.is_valid() || Entity.has<FTagDead>()) return false;

				FProjectileInstance* ProjInstance = Entity.try_get_mut<FProjectileInstance>();
				if (!ProjInstance) return false;

				// Skip bounce with own owner
				if (ProjInstance->OwnerEntityId != 0
					&& static_cast<uint64>(ProjInstance->OwnerEntityId) == OtherId)
				{
					return true;
				}

				const FProjectileStatic* ProjStatic = Entity.try_get<FProjectileStatic>();
				const int32 MaxBounces = ProjStatic ? ProjStatic->MaxBounces : -1;

				ProjInstance->BounceCount++;

				UE_LOG(LogTemp, Log, TEXT("COLLISION: Bounce %d/%d for Entity %llu"),
					ProjInstance->BounceCount, MaxBounces, EntityId);

				if (MaxBounces >= 0 && ProjInstance->BounceCount > MaxBounces)
				{
					// Store contact point for accurate death VFX position
					FDeathContactPoint DCP;
					DCP.Position = ContactPoint;
					Entity.set<FDeathContactPoint>(DCP);

					Entity.add<FTagDead>();
					UE_LOG(LogTemp, Log, TEXT("COLLISION: Projectile %llu exceeded max bounces at (%.0f,%.0f,%.0f)"),
						EntityId, ContactPoint.X, ContactPoint.Y, ContactPoint.Z);
				}

				return true;
			};

			if (!ProcessBounce(Pair.EntityId1, Pair.EntityId2))
			{
				ProcessBounce(Pair.EntityId2, Pair.EntityId1);
			}

			PairEntity.add<FTagCollisionProcessed>();
		});

	// ─────────────────────────────────────────────────────────
	// PICKUP COLLISION SYSTEM
	// Handles item pickup when character touches pickupable item.
	// Uses FWorldItemInstance for grace period check.
	// ─────────────────────────────────────────────────────────
	World.system<const FCollisionPair>("PickupCollisionSystem")
		.with<FTagCollisionPickup>()
		.without<FTagCollisionProcessed>()
		.each([&World](flecs::entity PairEntity, const FCollisionPair& Pair)
		{
			flecs::entity Entity1 = Pair.HasEntity1() ? World.entity(Pair.EntityId1) : flecs::entity();
			flecs::entity Entity2 = Pair.HasEntity2() ? World.entity(Pair.EntityId2) : flecs::entity();

			flecs::entity Character;
			flecs::entity Item;

			if (Entity1.is_valid() && Entity1.has<FTagCharacter>())
			{
				Character = Entity1;
				Item = Entity2;
			}
			else if (Entity2.is_valid() && Entity2.has<FTagCharacter>())
			{
				Character = Entity2;
				Item = Entity1;
			}

			if (Character.is_valid() && Item.is_valid() && !Item.has<FTagDead>())
			{
				// Check grace period
				const FWorldItemInstance* WorldItem = Item.try_get<FWorldItemInstance>();
				if (WorldItem && !WorldItem->CanBePickedUp())
				{
					PairEntity.add<FTagCollisionProcessed>();
					return;
				}

				UE_LOG(LogTemp, Log, TEXT("COLLISION: Pickup triggered - Character %llu touching Item %llu"),
					Character.id(), Item.id());

				// TODO: Transfer item to character's inventory
			}

			PairEntity.add<FTagCollisionProcessed>();
		});

	// ─────────────────────────────────────────────────────────
	// DESTRUCTIBLE COLLISION SYSTEM
	// Destroys FTagDestructible entities on projectile hit.
	// ─────────────────────────────────────────────────────────
	World.system<const FCollisionPair>("DestructibleCollisionSystem")
		.with<FTagCollisionDestructible>()
		.without<FTagCollisionProcessed>()
		.each([&World](flecs::entity PairEntity, const FCollisionPair& Pair)
		{
			auto TryDestroyDestructible = [&World](uint64 EntityId) -> bool
			{
				if (EntityId == 0) return false;

				flecs::entity Entity = World.entity(EntityId);
				if (!Entity.is_valid() || Entity.has<FTagDead>()) return false;

				if (Entity.has<FTagDestructible>())
				{
					Entity.add<FTagDead>();
					UE_LOG(LogTemp, Log, TEXT("COLLISION: Destructible %llu destroyed"), EntityId);
					return true;
				}
				return false;
			};

			TryDestroyDestructible(Pair.EntityId1);
			TryDestroyDestructible(Pair.EntityId2);

			PairEntity.add<FTagCollisionProcessed>();
		});

	// ─────────────────────────────────────────────────────────
	// FRAGMENTATION SYSTEM
	// Processes FTagCollisionFragmentation pairs.
	// Destroys the intact object and spawns pre-baked fragment bodies
	// from FDebrisPool, connected by fixed constraints.
	// ─────────────────────────────────────────────────────────
	// NOTE: No .without<FTagCollisionProcessed>() — fragmentation co-exists with damage.
	// A projectile hitting a fragmentable object triggers BOTH damage and fragmentation.
	World.system<const FCollisionPair, const FFragmentationData>("FragmentationSystem")
		.with<FTagCollisionFragmentation>()
		.each([this, &World](flecs::entity PairEntity, const FCollisionPair& Pair, const FFragmentationData& FragData)
		{
			EnsureBarrageAccess();

			FDebrisPool* Pool = GetDebrisPool();
			if (!Pool || !Pool->IsInitialized()) return;

			// Determine which entity is the destructible target
			uint64 TargetId = 0;
			FSkeletonKey TargetKey;
			if (Pair.HasEntity2())
			{
				flecs::entity E2 = World.entity(Pair.EntityId2);
				if (E2.is_valid() && E2.is_alive() && !E2.has<FTagDead>())
				{
					const FDestructibleStatic* Destr = E2.try_get<FDestructibleStatic>();
					if (Destr && Destr->IsValid())
					{
						TargetId = Pair.EntityId2;
						TargetKey = Pair.Key2;
					}
				}
			}
			if (TargetId == 0 && Pair.HasEntity1())
			{
				flecs::entity E1 = World.entity(Pair.EntityId1);
				if (E1.is_valid() && E1.is_alive() && !E1.has<FTagDead>())
				{
					const FDestructibleStatic* Destr = E1.try_get<FDestructibleStatic>();
					if (Destr && Destr->IsValid())
					{
						TargetId = Pair.EntityId1;
						TargetKey = Pair.Key1;
					}
				}
			}

			if (TargetId == 0)
			{
				PairEntity.add<FTagCollisionProcessed>();
				return;
			}

			flecs::entity TargetEntity = World.entity(TargetId);
			if (!TargetEntity.is_valid() || !TargetEntity.is_alive() || TargetEntity.has<FTagDead>())
			{
				PairEntity.add<FTagCollisionProcessed>();
				return;
			}

			const FDestructibleStatic* DestrStatic = TargetEntity.try_get<FDestructibleStatic>();
			if (!DestrStatic || !DestrStatic->IsValid())
			{
				PairEntity.add<FTagCollisionProcessed>();
				return;
			}

			UFlecsDestructibleProfile* Profile = DestrStatic->Profile;
			UFlecsDestructibleGeometry* Geometry = Profile->Geometry;
			if (!Geometry || Geometry->Fragments.Num() == 0)
			{
				PairEntity.add<FTagCollisionProcessed>();
				return;
			}

			// Get the intact object's world transform from Barrage
			FVector ObjectPosition = FVector::ZeroVector;
			FQuat ObjectRotation = FQuat::Identity;
			if (CachedBarrageDispatch && TargetKey.IsValid())
			{
				FBLet TargetPrim = CachedBarrageDispatch->GetShapeRef(TargetKey);
				if (FBarragePrimitive::IsNotNull(TargetPrim))
				{
					ObjectPosition = FVector(FBarragePrimitive::GetPosition(TargetPrim));
					ObjectRotation = FQuat(FBarragePrimitive::OptimisticGetAbsoluteRotation(TargetPrim));
				}
			}
			FTransform ObjectTransform(ObjectRotation, ObjectPosition);

			// ─────────────────────────────────────────────────────
			// Kill the intact object (ISM remove, DEBRIS layer, tombstone)
			// ─────────────────────────────────────────────────────
			TargetEntity.add<FTagDead>();

			UE_LOG(LogTemp, Log, TEXT("FRAGMENTATION: Destroying intact object %llu, spawning %d fragments at (%.0f,%.0f,%.0f)"),
				TargetId, Geometry->Fragments.Num(), ObjectPosition.X, ObjectPosition.Y, ObjectPosition.Z);

			// ─────────────────────────────────────────────────────
			// Spawn fragments
			// ─────────────────────────────────────────────────────
			// Arrays parallel to Geometry->Fragments — same indices.
			// Invalid entries for failed acquires or missing meshes.
			const int32 FragCount = Geometry->Fragments.Num();
			TArray<FSkeletonKey> FragmentKeys;
			TArray<flecs::entity> FragmentEntities;
			FragmentKeys.SetNumZeroed(FragCount);
			FragmentEntities.SetNum(FragCount);

			// World anchor mode: find ALL bottom-layer fragments (lowest Z within tolerance).
			// Each gets a breakable Fixed constraint to Body::sFixedToWorld.
			const bool bAnchorToWorld = Profile->bAnchorToWorld;
			TArray<int32> AnchorIndices;
			if (bAnchorToWorld)
			{
				// Find minimum Z across all fragments
				float LowestZ = TNumericLimits<float>::Max();
				for (int32 i = 0; i < FragCount; ++i)
				{
					const FDestructibleFragment& Frag = Geometry->Fragments[i];
					if (!Frag.Mesh) continue;
					const float FragZ = (Frag.RelativeTransform * ObjectTransform).GetLocation().Z;
					if (FragZ < LowestZ) LowestZ = FragZ;
				}
				// All fragments within 1cm of lowest Z are "bottom layer"
				constexpr float AnchorZTolerance = 1.0f;
				for (int32 i = 0; i < FragCount; ++i)
				{
					const FDestructibleFragment& Frag = Geometry->Fragments[i];
					if (!Frag.Mesh) continue;
					const float FragZ = (Frag.RelativeTransform * ObjectTransform).GetLocation().Z;
					if (FragZ <= LowestZ + AnchorZTolerance)
					{
						AnchorIndices.Add(i);
					}
				}
			}

			for (int32 FragIdx = 0; FragIdx < FragCount; ++FragIdx)
			{
				const FDestructibleFragment& Fragment = Geometry->Fragments[FragIdx];
				if (!Fragment.Mesh) continue;

				// Compute world transform for this fragment
				FTransform FragWorldTransform = Fragment.RelativeTransform * ObjectTransform;
				FVector FragPos = FragWorldTransform.GetLocation();
				FQuat FragRot = FragWorldTransform.GetRotation();

				// Acquire body from pool
				FSkeletonKey FragKey = Pool->Acquire(FragPos, FragRot);
				if (!FragKey.IsValid())
				{
					UE_LOG(LogTemp, Warning, TEXT("FRAGMENTATION: Failed to acquire debris body for fragment %d"), FragIdx);
					continue;
				}

				// Get the FBLet for reverse binding — must succeed for a just-acquired pool body
				FBLet FragPrim = CachedBarrageDispatch->GetShapeRef(FragKey);
				if (!FBarragePrimitive::IsNotNull(FragPrim))
				{
					UE_LOG(LogTemp, Error, TEXT("FRAGMENTATION: GetShapeRef null for just-acquired key — skipping fragment %d"), FragIdx);
					Pool->Release(FragKey);
					continue;
				}

				// Create Flecs entity for this fragment
				flecs::entity FragEntity = World.entity();

				FBarrageBody BarrageComp;
				BarrageComp.BarrageKey = FragKey;
				FragEntity.set<FBarrageBody>(BarrageComp);

				// Reverse binding
				FragPrim->SetFlecsEntity(FragEntity.id());

				// Debris instance data
				FDebrisInstance DebrisInst;
				DebrisInst.LifetimeRemaining = Profile->DebrisLifetime;
				DebrisInst.bAutoDestroy = Profile->bAutoDestroyDebris;
				DebrisInst.PoolSlotIndex = 0; // Marks as pooled (not INDEX_NONE)
				FragEntity.set<FDebrisInstance>(DebrisInst);

				FragEntity.add<FTagDebrisFragment>();

				// ISM render data
				FISMRender Render;
				Render.Mesh = Fragment.Mesh;
				Render.Scale = FragWorldTransform.GetScale3D();
				FragEntity.set<FISMRender>(Render);

				// Determine fragment definition (per-fragment override or default)
				UFlecsEntityDefinition* FragDef = Fragment.OverrideDefinition
					? Fragment.OverrideDefinition
					: Profile->DefaultFragmentDefinition;

				// Apply profiles from fragment definition (if any)
				if (FragDef)
				{
					if (FragDef->DamageProfile)
					{
						FDamageStatic DmgStatic;
						DmgStatic.Damage = FragDef->DamageProfile->Damage;
						DmgStatic.DamageType = FragDef->DamageProfile->DamageType;
						DmgStatic.bAreaDamage = FragDef->DamageProfile->bAreaDamage;
						DmgStatic.AreaRadius = FragDef->DamageProfile->AreaRadius;
						DmgStatic.bDestroyOnHit = FragDef->DamageProfile->bDestroyOnHit;
						DmgStatic.CritChance = FragDef->DamageProfile->CriticalChance;
						DmgStatic.CritMultiplier = FragDef->DamageProfile->CriticalMultiplier;
						FragEntity.set<FDamageStatic>(DmgStatic);
					}

					if (FragDef->HealthProfile)
					{
						FHealthStatic HealthStatic;
						HealthStatic.MaxHP = FragDef->HealthProfile->MaxHealth;
						HealthStatic.Armor = FragDef->HealthProfile->Armor;
						HealthStatic.bDestroyOnDeath = FragDef->HealthProfile->bDestroyOnDeath;
						FragEntity.set<FHealthStatic>(HealthStatic);

						FHealthInstance HealthInst;
						HealthInst.CurrentHP = FragDef->HealthProfile->GetStartingHealth();
						FragEntity.set<FHealthInstance>(HealthInst);
					}
				}

				// Queue ISM spawn for game thread
				FPendingFragmentSpawn ISMSpawn;
				ISMSpawn.EntityKey = FragKey;
				ISMSpawn.Mesh = Fragment.Mesh;
				ISMSpawn.Material = (FragDef && FragDef->RenderProfile)
					? FragDef->RenderProfile->MaterialOverride
					: nullptr;
				ISMSpawn.WorldTransform = FragWorldTransform;
				PendingFragmentSpawns.Enqueue(ISMSpawn);

				// Niagara attached effects on fragments
				if (FragDef && FragDef->NiagaraProfile && FragDef->NiagaraProfile->HasAttachedEffect())
				{
					// Queue Niagara registration via pending spawn system
					// (NiagaraManager handles this when ISM is created on game thread)
				}

				// Death VFX on fragment death
				if (FragDef && FragDef->NiagaraProfile && FragDef->NiagaraProfile->HasDeathEffect())
				{
					FNiagaraDeathEffect DeathVFX;
					DeathVFX.Effect = FragDef->NiagaraProfile->DeathEffect;
					DeathVFX.Scale = FragDef->NiagaraProfile->DeathEffectScale;
					FragEntity.set<FNiagaraDeathEffect>(DeathVFX);
				}

				FragmentKeys[FragIdx] = FragKey;
				FragmentEntities[FragIdx] = FragEntity;
			}

			// ─────────────────────────────────────────────────────
			// Create constraints from adjacency graph
			// ─────────────────────────────────────────────────────
			FBarrageConstraintSystem* ConstraintSys = CachedBarrageDispatch
				? CachedBarrageDispatch->GetConstraintSystem()
				: nullptr;

			UE_LOG(LogTemp, Warning, TEXT("FRAGMENTATION: === CONSTRAINT CREATION START ==="));
			UE_LOG(LogTemp, Warning, TEXT("FRAGMENTATION: Geometry=%s, AdjacencyLinks=%d, ConstraintSys=%s, BreakForce=%.1f, BreakTorque=%.1f"),
				*Geometry->GetName(), Geometry->AdjacencyLinks.Num(),
				ConstraintSys ? TEXT("valid") : TEXT("NULL"),
				Profile->ConstraintBreakForce, Profile->ConstraintBreakTorque);

			// Log all fragment keys
			for (int32 i = 0; i < FragmentKeys.Num(); ++i)
			{
				UE_LOG(LogTemp, Warning, TEXT("FRAGMENTATION:   FragKey[%d] valid=%s, Entity valid=%s"),
					i,
					FragmentKeys[i].IsValid() ? TEXT("YES") : TEXT("NO"),
					FragmentEntities[i].is_valid() ? TEXT("YES") : TEXT("NO"));
			}

			if (ConstraintSys && Geometry->AdjacencyLinks.Num() > 0)
			{
				int32 ConstraintsCreated = 0;
				int32 SkippedIndex = 0, SkippedKey = 0, SkippedPrim = 0, SkippedBarrage = 0, SkippedCreate = 0;

				for (const FFragmentAdjacency& Link : Geometry->AdjacencyLinks)
				{
					UE_LOG(LogTemp, Warning, TEXT("FRAGMENTATION:   Link [%d]-[%d]:"), Link.FragmentIndexA, Link.FragmentIndexB);

					if (Link.FragmentIndexA >= FragmentKeys.Num() || Link.FragmentIndexB >= FragmentKeys.Num())
					{
						UE_LOG(LogTemp, Error, TEXT("FRAGMENTATION:     SKIP index out of range (max=%d)"), FragmentKeys.Num());
						++SkippedIndex;
						continue;
					}

					FSkeletonKey KeyA = FragmentKeys[Link.FragmentIndexA];
					FSkeletonKey KeyB = FragmentKeys[Link.FragmentIndexB];
					if (!KeyA.IsValid() || !KeyB.IsValid())
					{
						UE_LOG(LogTemp, Error, TEXT("FRAGMENTATION:     SKIP invalid SkeletonKey A=%s B=%s"),
							KeyA.IsValid() ? TEXT("ok") : TEXT("INVALID"),
							KeyB.IsValid() ? TEXT("ok") : TEXT("INVALID"));
						++SkippedKey;
						continue;
					}

					FBLet PrimA = CachedBarrageDispatch->GetShapeRef(KeyA);
					FBLet PrimB = CachedBarrageDispatch->GetShapeRef(KeyB);
					if (!FBarragePrimitive::IsNotNull(PrimA) || !FBarragePrimitive::IsNotNull(PrimB))
					{
						UE_LOG(LogTemp, Error, TEXT("FRAGMENTATION:     SKIP GetShapeRef failed A=%s B=%s"),
							FBarragePrimitive::IsNotNull(PrimA) ? TEXT("ok") : TEXT("NULL"),
							FBarragePrimitive::IsNotNull(PrimB) ? TEXT("ok") : TEXT("NULL"));
						++SkippedPrim;
						continue;
					}

					FBarrageKey BodyA = PrimA->KeyIntoBarrage;
					FBarrageKey BodyB = PrimB->KeyIntoBarrage;
					UE_LOG(LogTemp, Warning, TEXT("FRAGMENTATION:     BarrageKey A=%llu B=%llu"),
						BodyA.KeyIntoBarrage, BodyB.KeyIntoBarrage);

					if (BodyA.KeyIntoBarrage == 0 || BodyB.KeyIntoBarrage == 0)
					{
						UE_LOG(LogTemp, Error, TEXT("FRAGMENTATION:     SKIP BarrageKey is 0"));
						++SkippedBarrage;
						continue;
					}

					FBarrageConstraintKey CKey = CachedBarrageDispatch->CreateFixedConstraint(
						BodyA, BodyB, Profile->ConstraintBreakForce, Profile->ConstraintBreakTorque);

					UE_LOG(LogTemp, Warning, TEXT("FRAGMENTATION:     CreateFixedConstraint -> Key=%llu valid=%s"),
						CKey.Key, CKey.IsValid() ? TEXT("YES") : TEXT("NO"));

					if (CKey.IsValid())
					{
						++ConstraintsCreated;

						auto RegisterConstraint = [&](flecs::entity E, FSkeletonKey OtherKey)
						{
							FFlecsConstraintData& Data = E.obtain<FFlecsConstraintData>();
							Data.AddConstraint(CKey.Key, OtherKey, Profile->ConstraintBreakForce, Profile->ConstraintBreakTorque);
							E.add<FTagConstrained>();
						};

						flecs::entity EA = FragmentEntities[Link.FragmentIndexA];
						flecs::entity EB = FragmentEntities[Link.FragmentIndexB];
						if (EA.is_valid()) RegisterConstraint(EA, KeyB);
						if (EB.is_valid()) RegisterConstraint(EB, KeyA);
					}
					else
					{
						++SkippedCreate;
					}
				}

				UE_LOG(LogTemp, Warning, TEXT("FRAGMENTATION: === RESULT: %d/%d constraints created ==="),
					ConstraintsCreated, Geometry->AdjacencyLinks.Num());
				if (SkippedIndex + SkippedKey + SkippedPrim + SkippedBarrage + SkippedCreate > 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("FRAGMENTATION:   Skips: Index=%d Key=%d Prim=%d Barrage=%d Create=%d"),
						SkippedIndex, SkippedKey, SkippedPrim, SkippedBarrage, SkippedCreate);
				}
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("FRAGMENTATION: NO CONSTRAINTS — ConstraintSys=%s, AdjacencyLinks=%d"),
					ConstraintSys ? TEXT("valid") : TEXT("NULL"), Geometry->AdjacencyLinks.Num());
			}

			// ─────────────────────────────────────────────────────
			// World anchor constraints — pin bottom fragments to world
			// ─────────────────────────────────────────────────────
			if (bAnchorToWorld && AnchorIndices.Num() > 0 && ConstraintSys)
			{
				int32 WorldConstraintsCreated = 0;
				const FBarrageKey InvalidWorldBody; // KeyIntoBarrage == 0 → Body::sFixedToWorld

				for (int32 AnchorFragIdx : AnchorIndices)
				{
					FSkeletonKey FragKey = FragmentKeys[AnchorFragIdx];
					if (!FragKey.IsValid()) continue;

					FBLet FragPrim = CachedBarrageDispatch->GetShapeRef(FragKey);
					if (!FBarragePrimitive::IsNotNull(FragPrim)) continue;

					FBarrageConstraintKey CKey = CachedBarrageDispatch->CreateFixedConstraint(
						FragPrim->KeyIntoBarrage, InvalidWorldBody,
						Profile->AnchorBreakForce, Profile->AnchorBreakTorque);

					if (CKey.IsValid())
					{
						++WorldConstraintsCreated;

						// Register on the fragment entity so ConstraintBreakSystem can track it
						flecs::entity FragEntity = FragmentEntities[AnchorFragIdx];
						if (FragEntity.is_valid())
						{
							FFlecsConstraintData& Data = FragEntity.obtain<FFlecsConstraintData>();
							Data.AddConstraint(CKey.Key, FSkeletonKey(), Profile->AnchorBreakForce, Profile->AnchorBreakTorque);
							FragEntity.add<FTagConstrained>();
						}
					}
				}

				UE_LOG(LogTemp, Log, TEXT("FRAGMENTATION: Created %d world anchor constraints for %d bottom fragments (BreakForce=%.0f)"),
					WorldConstraintsCreated, AnchorIndices.Num(), Profile->AnchorBreakForce);
			}

			// ─────────────────────────────────────────────────────
			// Apply impulse to nearest fragment
			// ─────────────────────────────────────────────────────
			if (FragData.ImpactImpulse > 0.f)
			{
				const FVector ImpulseDir = FragData.ImpactDirection.IsNearlyZero()
					? FVector::UpVector
					: FragData.ImpactDirection;
				const float BaseImpulse = FragData.ImpactImpulse * Profile->ImpulseMultiplier;

				float BestDistSq = TNumericLimits<float>::Max();
				int32 BestIdx = INDEX_NONE;
				for (int32 i = 0; i < FragCount; ++i)
				{
					if (!FragmentKeys[i].IsValid()) continue;

					const float DistSq = FVector::DistSquared(
						(Geometry->Fragments[i].RelativeTransform * ObjectTransform).GetLocation(),
						FragData.ImpactPoint);
					if (DistSq < BestDistSq)
					{
						BestDistSq = DistSq;
						BestIdx = i;
					}
				}
				if (BestIdx != INDEX_NONE)
				{
					FBLet NearestPrim = CachedBarrageDispatch->GetShapeRef(FragmentKeys[BestIdx]);
					if (FBarragePrimitive::IsNotNull(NearestPrim))
					{
						FBarragePrimitive::SetVelocity(
							FVector3d(ImpulseDir * BaseImpulse), NearestPrim);
					}
				}
			}

			PairEntity.add<FTagCollisionProcessed>();
		});

	// ═══════════════════════════════════════════════════════════════
	// WEAPON SYSTEMS
	// ═══════════════════════════════════════════════════════════════

	// ─────────────────────────────────────────────────────────
	// WEAPON TICK SYSTEM
	// Updates timers for fire rate, burst cooldown, and semi-auto reset.
	// ─────────────────────────────────────────────────────────
	World.system<FWeaponInstance>("WeaponTickSystem")
		.with<FTagWeapon>()
		.without<FTagDead>()
		.each([](flecs::entity Entity, FWeaponInstance& Weapon)
		{
			const float DeltaTime = Entity.world().get_info()->delta_time;

			// Countdown fire cooldown (subtract from clean value, no accumulation error)
			if (Weapon.FireCooldownRemaining > 0.f)
			{
				Weapon.FireCooldownRemaining -= DeltaTime;
			}

			// Update burst cooldown
			if (Weapon.BurstCooldownRemaining > 0.f)
			{
				Weapon.BurstCooldownRemaining -= DeltaTime;
			}

			// Reset semi-auto flag when trigger released
			if (!Weapon.bFireRequested && Weapon.bHasFiredSincePress)
			{
				Weapon.bHasFiredSincePress = false;
			}
		});

	// ─────────────────────────────────────────────────────────
	// WEAPON RELOAD SYSTEM
	// Handles reload state machine.
	// ─────────────────────────────────────────────────────────
	World.system<FWeaponInstance>("WeaponReloadSystem")
		.with<FTagWeapon>()
		.without<FTagDead>()
		.each([](flecs::entity Entity, FWeaponInstance& Weapon)
		{
			const float DeltaTime = Entity.world().get_info()->delta_time;

			const FWeaponStatic* Static = Entity.try_get<FWeaponStatic>();
			if (!Static) return;

			// Process reload request
			if (Weapon.bReloadRequested && !Weapon.bIsReloading)
			{
				if (Weapon.CanReload(Static->MagazineSize, Static->bUnlimitedAmmo))
				{
					Weapon.bIsReloading = true;
					Weapon.ReloadTimeRemaining = Static->ReloadTime;
					UE_LOG(LogTemp, Log, TEXT("WEAPON: Reload started, %.2f sec"), Static->ReloadTime);

					if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
					{
						FUIReloadMessage ReloadMsg;
						ReloadMsg.WeaponEntityId = static_cast<int64>(Entity.id());
						ReloadMsg.bStarted = true;
						ReloadMsg.MagazineSize = Static->MagazineSize;
						MsgSub->EnqueueMessage(TAG_UI_Reload, ReloadMsg);
					}
				}
				Weapon.bReloadRequested = false;
			}

			// Process reload timer
			if (Weapon.bIsReloading)
			{
				Weapon.ReloadTimeRemaining -= DeltaTime;

				if (Weapon.ReloadTimeRemaining <= 0.f)
				{
					// Complete reload
					int32 AmmoNeeded = Static->MagazineSize - Weapon.CurrentAmmo;

					if (Static->bUnlimitedAmmo)
					{
						Weapon.CurrentAmmo = Static->MagazineSize;
					}
					else
					{
						int32 AmmoToLoad = FMath::Min(AmmoNeeded, Weapon.ReserveAmmo);
						Weapon.CurrentAmmo += AmmoToLoad;
						Weapon.ReserveAmmo -= AmmoToLoad;
					}

					Weapon.bIsReloading = false;
					UE_LOG(LogTemp, Log, TEXT("WEAPON: Reload complete, ammo=%d/%d reserve=%d"),
						Weapon.CurrentAmmo, Static->MagazineSize, Weapon.ReserveAmmo);

					if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
					{
						FUIReloadMessage ReloadMsg;
						ReloadMsg.WeaponEntityId = static_cast<int64>(Entity.id());
						ReloadMsg.bStarted = false;
						ReloadMsg.NewAmmo = Weapon.CurrentAmmo;
						ReloadMsg.MagazineSize = Static->MagazineSize;
						MsgSub->EnqueueMessage(TAG_UI_Reload, ReloadMsg);
					}
				}
			}
		});

	// ─────────────────────────────────────────────────────────
	// WEAPON FIRE SYSTEM
	// Processes fire requests for equipped weapons.
	// Queues projectile spawns for game thread processing.
	// ─────────────────────────────────────────────────────────
	World.system<FWeaponInstance, const FEquippedBy>("WeaponFireSystem")
		.with<FTagWeapon>()
		.without<FTagDead>()
		.each([this, &World](flecs::entity WeaponEntity, FWeaponInstance& Weapon, const FEquippedBy& EquippedBy)
		{
			// Flecs worker threads need Barrage access for physics lookups
			EnsureBarrageAccess();

			// Only process equipped weapons
			if (!EquippedBy.IsEquipped()) return;

			const FWeaponStatic* Static = WeaponEntity.try_get<FWeaponStatic>();
			if (!Static || !Static->ProjectileDefinition) return;

			// Check fire request: continuous hold OR pending trigger (survives Start+Stop batching)
			if (!Weapon.bFireRequested && !Weapon.bFireTriggerPending) return;

			// Semi-auto: block if already fired while trigger held
			if (!Static->bIsAutomatic && !Static->bIsBurst && Weapon.bHasFiredSincePress)
			{
				UE_LOG(LogTemp, Warning, TEXT("WEAPON: Blocked by semi-auto (bHasFiredSincePress=true)"));
				return;
			}

			// Check if can fire (cooldown expired, has ammo, not reloading)
			if (!Weapon.CanFire())
			{
				return;
			}

			// Get character entity - if dead or invalid, stop firing
			flecs::entity CharacterEntity = World.entity(static_cast<flecs::entity_t>(EquippedBy.CharacterEntityId));
			if (!CharacterEntity.is_valid() || !CharacterEntity.is_alive() || CharacterEntity.has<FTagDead>())
			{
				Weapon.bFireRequested = false;
				Weapon.bFireTriggerPending = false;
				return;
			}

			const FBarrageBody* CharBody = CharacterEntity.try_get<FBarrageBody>();
			if (!CharBody || !CharBody->IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("WEAPON: CharBody null or invalid! HasComponent=%d"), CharBody != nullptr);
				return;
			}

			// MUZZLE CALCULATION — reads aim state from FAimDirection
			// MuzzleWorldPosition is the actual weapon socket position (follows animations).
			// CharacterPosition is camera position (for aim raycast origin).
			FVector MuzzleLocation = FVector::ZeroVector;
			FVector FireDirection = FVector::ForwardVector;
			FVector CharPosD = FVector::ZeroVector;

			const FAimDirection* AimDir = CharacterEntity.try_get<FAimDirection>();
			if (AimDir)
			{
				if (!AimDir->Direction.IsNearlyZero())
				{
					FireDirection = AimDir->Direction;
				}
				CharPosD = AimDir->CharacterPosition;

				if (!AimDir->MuzzleWorldPosition.IsNearlyZero())
				{
					MuzzleLocation = AimDir->MuzzleWorldPosition;
				}
			}

			// Fallback: compute from camera + weapon static offset (no weapon mesh socket)
			if (MuzzleLocation.IsNearlyZero())
			{
				FQuat AimQuat = FRotationMatrix::MakeFromX(FireDirection).ToQuat();
				FTransform MuzzleTransform(AimQuat, CharPosD);
				MuzzleLocation = MuzzleTransform.TransformPosition(Static->MuzzleOffset);
			}

			// ─────────────────────────────────────────────────────
			// PROJECTILE CREATION (on sim thread — no game thread round-trip)
			// Creates Barrage body + Flecs entity immediately.
			// Only ISM render queued for game thread.
			// ─────────────────────────────────────────────────────
			UFlecsEntityDefinition* ProjDef = Static->ProjectileDefinition;
			UFlecsProjectileProfile* ProjProfile = ProjDef->ProjectileProfile;
			if (!ProjProfile)
			{
				UE_LOG(LogTemp, Error, TEXT("WEAPON: ProjectileDefinition '%s' has no ProjectileProfile!"),
					*ProjDef->EntityName.ToString());
				return;
			}
			UFlecsPhysicsProfile* PhysProfile = ProjDef->PhysicsProfile;
			UFlecsRenderProfile* RenderProfile = ProjDef->RenderProfile;

			const float CollisionRadius = PhysProfile ? PhysProfile->CollisionRadius : 30.f;
			const float GravityFactor = PhysProfile ? PhysProfile->GravityFactor : 0.f;
			const float ProjFriction = PhysProfile ? PhysProfile->Friction : 0.2f;
			const float ProjRestitution = PhysProfile ? PhysProfile->Restitution : 0.3f;
			const float ProjLinearDamping = PhysProfile ? PhysProfile->LinearDamping : 0.0f;
			const bool bIsBouncing = ProjProfile->IsBouncing();
			// ALL projectiles use dynamic body — sensors tunnel at high speed (no CCD).
			// Non-bouncing non-gravity projectiles: dynamic + restitution=0 + gravity=0.
			const bool bNeedsDynamic = true;

			// ─────────────────────────────────────────────────────
			// AIM CORRECTION: Raycast from camera to find actual target,
			// then compute barrel→target direction. Projectile spawns
			// from barrel AND flies exactly where the crosshair points.
			// ─────────────────────────────────────────────────────
			const float AimTraceRange = 100000.f; // 1km
			FVector TargetPoint = CharPosD + FireDirection * AimTraceRange;

			{
				FBLet CharPrim = CachedBarrageDispatch->GetShapeRef(CharBody->BarrageKey);
				if (FBarragePrimitive::IsNotNull(CharPrim))
				{
					auto BPFilter = CachedBarrageDispatch->GetDefaultBroadPhaseLayerFilter(Layers::CAST_QUERY);
					FastExcludeObjectLayerFilter ObjFilter({
						EPhysicsLayer::PROJECTILE,
						EPhysicsLayer::ENEMYPROJECTILE,
						EPhysicsLayer::DEBRIS
					});
					auto BodyFilter = CachedBarrageDispatch->GetFilterToIgnoreSingleBody(CharPrim);

					TSharedPtr<FHitResult> AimHit = MakeShared<FHitResult>();
					CachedBarrageDispatch->CastRay(
						CharPosD,
						FireDirection * AimTraceRange,
						BPFilter, ObjFilter, BodyFilter,
						AimHit);

					if (AimHit->bBlockingHit)
					{
						TargetPoint = AimHit->ImpactPoint;
					}
				}
			}

			// Minimum engagement distance: if target too close to camera,
			// push it along aim ray to prevent barrel parallax issues.
			constexpr float MinEngagementDist = 300.f; // 3m
			if (FVector::DistSquared(CharPosD, TargetPoint) < MinEngagementDist * MinEngagementDist)
			{
				TargetPoint = CharPosD + FireDirection * MinEngagementDist;
			}

			// Direction from barrel to target (accounts for barrel offset at any distance)
			FVector SpawnDirection = (TargetPoint - MuzzleLocation).GetSafeNormal();

			// Dot product safety: if barrel→target diverges too much from aim,
			// fall back to aim direction (catches extreme edge cases).
			if (FVector::DotProduct(SpawnDirection, FireDirection) < 0.85f) // > ~32° deviation
			{
				SpawnDirection = FireDirection;
			}

			float Speed = ProjProfile->DefaultSpeed * Static->ProjectileSpeedMultiplier;
			FVector Velocity = SpawnDirection * Speed;

			for (int32 i = 0; i < Static->ProjectilesPerShot; ++i)
			{
				FSkeletonKey ProjectileKey = FBarrageSpawnUtils::GenerateUniqueKey(SKELLY::SFIX_GUN_SHOT);

				// Create Barrage physics body
				FBSphereParams SphereParams = FBarrageBounder::GenerateSphereBounds(MuzzleLocation, CollisionRadius);
				FBLet Body;

				if (bNeedsDynamic)
				{
					Body = CachedBarrageDispatch->CreateBouncingSphere(
						SphereParams, ProjectileKey,
						static_cast<uint16>(EPhysicsLayer::PROJECTILE),
						bIsBouncing ? ProjRestitution : 0.f, ProjFriction, ProjLinearDamping);
				}
				else
				{
					Body = CachedBarrageDispatch->CreatePrimitive(
						SphereParams, ProjectileKey,
						static_cast<uint16>(EPhysicsLayer::PROJECTILE), true);
				}

				if (!FBarragePrimitive::IsNotNull(Body))
				{
					UE_LOG(LogTemp, Error, TEXT("WEAPON: Failed to create projectile body!"));
					continue;
				}

				FBarragePrimitive::SetVelocity(Velocity, Body);
				FBarragePrimitive::SetGravityFactor(GravityFactor, Body);

				// Create Flecs entity with components (no prefab — avoids deferred timing issues)
				flecs::entity ProjEntity = World.entity();

				FBarrageBody BarrageComp;
				BarrageComp.BarrageKey = ProjectileKey;
				ProjEntity.set<FBarrageBody>(BarrageComp);

				// Reverse binding (atomic in FBarragePrimitive)
				Body->SetFlecsEntity(ProjEntity.id());

				FProjectileInstance ProjInst;
				ProjInst.LifetimeRemaining = ProjProfile->Lifetime;
				ProjInst.BounceCount = 0;
				ProjInst.GraceFramesRemaining = ProjProfile->GetGraceFrames();
				ProjInst.OwnerEntityId = EquippedBy.CharacterEntityId;
				ProjEntity.set<FProjectileInstance>(ProjInst);
				ProjEntity.add<FTagProjectile>();

				// Static data directly on entity (projectile-specific, no prefab sharing needed)
				if (ProjProfile)
				{
					FProjectileStatic ProjStatic;
					ProjStatic.MaxLifetime = ProjProfile->Lifetime;
					ProjStatic.MaxBounces = ProjProfile->MaxBounces;
					ProjStatic.GracePeriodFrames = ProjProfile->GetGraceFrames();
					ProjStatic.MinVelocity = ProjProfile->MinVelocity;
					ProjEntity.set<FProjectileStatic>(ProjStatic);
				}

				if (ProjDef->DamageProfile)
				{
					FDamageStatic DmgStatic;
					DmgStatic.Damage = ProjDef->DamageProfile->Damage;
					DmgStatic.DamageType = ProjDef->DamageProfile->DamageType;
					DmgStatic.bAreaDamage = ProjDef->DamageProfile->bAreaDamage;
					DmgStatic.AreaRadius = ProjDef->DamageProfile->AreaRadius;
					DmgStatic.bDestroyOnHit = ProjDef->DamageProfile->bDestroyOnHit;
					DmgStatic.CritChance = ProjDef->DamageProfile->CriticalChance;
					DmgStatic.CritMultiplier = ProjDef->DamageProfile->CriticalMultiplier;
					ProjEntity.set<FDamageStatic>(DmgStatic);
				}

				if (RenderProfile && RenderProfile->Mesh)
				{
					FISMRender Render;
					Render.Mesh = RenderProfile->Mesh;
					Render.Scale = RenderProfile->Scale;
					ProjEntity.set<FISMRender>(Render);

					// Queue ISM render for game thread
					FPendingProjectileSpawn RenderSpawn;
					RenderSpawn.Mesh = RenderProfile->Mesh;
					RenderSpawn.Material = RenderProfile->MaterialOverride;
					RenderSpawn.Scale = RenderProfile->Scale;
					RenderSpawn.RotationOffset = RenderProfile->RotationOffset;
					RenderSpawn.SpawnDirection = SpawnDirection;
					RenderSpawn.SimComputedLocation = MuzzleLocation;
					RenderSpawn.EntityKey = ProjectileKey;

					// Niagara VFX fields (attached effect)
					if (ProjDef->NiagaraProfile && ProjDef->NiagaraProfile->HasAttachedEffect())
					{
						RenderSpawn.NiagaraEffect = ProjDef->NiagaraProfile->AttachedEffect;
						RenderSpawn.NiagaraScale = ProjDef->NiagaraProfile->AttachedEffectScale;
						RenderSpawn.NiagaraOffset = ProjDef->NiagaraProfile->AttachedOffset;
					}

					PendingProjectileSpawns.Enqueue(RenderSpawn);
				}

				// Death VFX component (read by DeadEntityCleanupSystem on death)
				if (ProjDef->NiagaraProfile && ProjDef->NiagaraProfile->HasDeathEffect())
				{
					FNiagaraDeathEffect DeathVFX;
					DeathVFX.Effect = ProjDef->NiagaraProfile->DeathEffect;
					DeathVFX.Scale = ProjDef->NiagaraProfile->DeathEffectScale;
					ProjEntity.set<FNiagaraDeathEffect>(DeathVFX);
				}

				UE_LOG(LogTemp, Log, TEXT("WEAPON: Projectile Key=%llu Entity=%llu AimDir=(%.3f,%.3f,%.3f) SpawnDir=(%.3f,%.3f,%.3f) Speed=%.0f Gravity=%.2f Target=(%.0f,%.0f,%.0f)"),
					static_cast<uint64>(ProjectileKey), ProjEntity.id(),
					FireDirection.X, FireDirection.Y, FireDirection.Z,
					SpawnDirection.X, SpawnDirection.Y, SpawnDirection.Z,
					Speed, GravityFactor,
					TargetPoint.X, TargetPoint.Y, TargetPoint.Z);
			}

			// Consume ammo
			int32 AmmoBefore = Weapon.CurrentAmmo;
			if (!Static->bUnlimitedAmmo)
			{
				Weapon.CurrentAmmo -= Static->AmmoPerShot;
				Weapon.CurrentAmmo = FMath::Max(0, Weapon.CurrentAmmo);
			}

			UE_LOG(LogTemp, Log, TEXT("WEAPON: FIRED! Ammo=%d->%d, Auto=%d, Burst=%d"),
				AmmoBefore, Weapon.CurrentAmmo,
				Static->bIsAutomatic, Static->bIsBurst);

			// Broadcast ammo change to message system (sim→game thread)
			if (auto* MsgSub = UFlecsMessageSubsystem::SelfPtr)
			{
				FUIAmmoMessage AmmoMsg;
				AmmoMsg.WeaponEntityId = static_cast<int64>(WeaponEntity.id());
				AmmoMsg.CurrentAmmo = Weapon.CurrentAmmo;
				AmmoMsg.MagazineSize = Static->MagazineSize;
				AmmoMsg.ReserveAmmo = Weapon.ReserveAmmo;
				MsgSub->EnqueueMessage(TAG_UI_Ammo, AmmoMsg);
			}

			// Carry-over overshoot for consistent average fire rate.
			// If cooldown was -0.003 when we fire, += FireInterval gives 0.097
			// instead of 0.1, compensating for the overshoot.
			Weapon.FireCooldownRemaining += Static->FireInterval;

			// Consume pending trigger (one shot per click guaranteed)
			Weapon.bFireTriggerPending = false;

			// Mark as fired for semi-auto
			Weapon.bHasFiredSincePress = true;

			// Handle burst mode
			if (Static->bIsBurst)
			{
				if (Weapon.BurstShotsRemaining == 0)
				{
					// Starting new burst
					Weapon.BurstShotsRemaining = Static->BurstCount - 1;
				}
				else
				{
					Weapon.BurstShotsRemaining--;
					if (Weapon.BurstShotsRemaining == 0)
					{
						// Burst complete, enter cooldown
						Weapon.BurstCooldownRemaining = Static->BurstDelay;
						Weapon.bHasFiredSincePress = true; // Block until trigger release
					}
				}
			}

			// Auto-reload when empty
			if (Weapon.CurrentAmmo == 0 && !Static->bUnlimitedAmmo && Weapon.ReserveAmmo > 0)
			{
				Weapon.bReloadRequested = true;
			}
		});

	// ═══════════════════════════════════════════════════════════════
	// CLEANUP SYSTEMS
	// ═══════════════════════════════════════════════════════════════

	// ─────────────────────────────────────────────────────────
	// DEATH CHECK SYSTEM
	// Entities with FHealthInstance that have CurrentHP <= 0.
	// ─────────────────────────────────────────────────────────
	World.system<const FHealthInstance>("DeathCheckSystem")
		.without<FTagDead>()
		.each([](flecs::entity Entity, const FHealthInstance& Health)
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
			}
		});

	// ─────────────────────────────────────────────────────────
	// DEAD ENTITY CLEANUP SYSTEM
	// Entities tagged FTagDead get fully cleaned up:
	// 1. Remove ISM render instance
	// 2. Move physics body to DEBRIS layer
	// 3. Mark for deferred destruction via tombstone
	// 4. Destroy Flecs entity
	// ─────────────────────────────────────────────────────────
	World.system<>("DeadEntityCleanupSystem")
		.with<FTagDead>()
		.each([this](flecs::entity Entity)
		{
			EnsureBarrageAccess();

			// ─────────────────────────────────────────────────────
			// CONSTRAINT CLEANUP: Remove all constraints for this entity
			// Must happen before body cleanup (constraints reference bodies).
			// ─────────────────────────────────────────────────────
			if (Entity.has<FTagConstrained>() && CachedBarrageDispatch)
			{
				const FBarrageBody* ConstrainedBody = Entity.try_get<FBarrageBody>();
				if (ConstrainedBody && ConstrainedBody->IsValid())
				{
					// Use GetShapeRef→KeyIntoBarrage (pool bodies aren't in TranslationMapping)
					FBLet ConstrainedPrim = CachedBarrageDispatch->GetShapeRef(ConstrainedBody->BarrageKey);
					if (FBarragePrimitive::IsNotNull(ConstrainedPrim))
					{
						FBarrageConstraintSystem* ConstraintSys = CachedBarrageDispatch->GetConstraintSystem();
						if (ConstraintSys)
						{
							ConstraintSys->RemoveAllForBody(ConstrainedPrim->KeyIntoBarrage);
						}
					}
				}
			}

			const FBarrageBody* Body = Entity.try_get<FBarrageBody>();

			if (Body && Body->IsValid())
			{
				FSkeletonKey Key = Body->BarrageKey;

				// Remove ISM render instance
				if (CachedBarrageDispatch && CachedBarrageDispatch->GetWorld())
				{
					if (UFlecsRenderManager* Renderer = UFlecsRenderManager::Get(CachedBarrageDispatch->GetWorld()))
					{
						Renderer->RemoveInstance(Key);
					}
				}

				// Queue death VFX + unregister from Niagara (MPSC → game thread)
				if (UFlecsNiagaraManager* NiagaraMgr = UFlecsNiagaraManager::Get(
						CachedBarrageDispatch ? CachedBarrageDispatch->GetWorld() : nullptr))
				{
					const FNiagaraDeathEffect* DeathVFX = Entity.try_get<FNiagaraDeathEffect>();
					if (DeathVFX && DeathVFX->Effect && CachedBarrageDispatch)
					{
						FPendingDeathEffect FX;
						FX.Effect = DeathVFX->Effect;
						FX.Scale = DeathVFX->Scale;

						// Prefer stored contact point (accurate impact position).
						// Physics body position is WRONG after StepWorld resolves collision.
						const FDeathContactPoint* DCP = Entity.try_get<FDeathContactPoint>();
						if (DCP && !DCP->Position.IsNearlyZero())
						{
							FX.Location = DCP->Position;
						}
						else
						{
							// Fallback: read from physics (for non-collision deaths like lifetime expiry)
							FBLet DeathPrim = CachedBarrageDispatch->GetShapeRef(Key);
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
								// No physics body — skip VFX, just unregister
								NiagaraMgr->EnqueueRemoval(Key);
								Entity.destruct();
								return;
							}
						}

						NiagaraMgr->EnqueueDeathEffect(FX);
					}
					NiagaraMgr->EnqueueRemoval(Key);
				}

				// ─────────────────────────────────────────────────
				// POOLED DEBRIS: Release back to pool instead of tombstone
				// ─────────────────────────────────────────────────
				const FDebrisInstance* Debris = Entity.try_get<FDebrisInstance>();
				if (Debris && Debris->PoolSlotIndex != INDEX_NONE)
				{
					FDebrisPool* Pool = GetDebrisPool();
					if (Pool && Pool->IsInitialized())
					{
						FBLet Prim = CachedBarrageDispatch->GetShapeRef(Key);
						if (FBarragePrimitive::IsNotNull(Prim))
						{
							Prim->ClearFlecsEntity();
						}
						Pool->Release(Key);
						Entity.destruct();
						return; // Skip normal tombstone path
					}
				}

				// Handle Barrage physics body cleanup (normal path)
				if (CachedBarrageDispatch)
				{
					FBLet Prim = CachedBarrageDispatch->GetShapeRef(Key);
					if (FBarragePrimitive::IsNotNull(Prim))
					{
						Prim->ClearFlecsEntity();
						FBarrageKey BarrageKey = Prim->KeyIntoBarrage;
						CachedBarrageDispatch->SetBodyObjectLayer(BarrageKey, Layers::DEBRIS);
						CachedBarrageDispatch->SuggestTombstone(Prim);
					}
				}
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
