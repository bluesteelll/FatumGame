// FlecsArtillerySubsystem - Flecs Systems Setup

#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FlecsGameTags.h"
#include "FlecsStaticComponents.h"
#include "FlecsInstanceComponents.h"
#include "EPhysicsLayer.h"
#include "Systems/BarrageEntitySpawner.h"

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
	World.component<FTagConstrained>();

	// ─────────────────────────────────────────────────────────
	// COLLISION PAIR SYSTEM COMPONENTS
	// ─────────────────────────────────────────────────────────
	World.component<FCollisionPair>();
	World.component<FTagCollisionDamage>();
	World.component<FTagCollisionPickup>();
	World.component<FTagCollisionBounce>();
	World.component<FTagCollisionDestructible>();
	World.component<FTagCollisionCharacter>();
	World.component<FTagCollisionProcessed>();

	// ─────────────────────────────────────────────────────────
	// CONSTRAINT COMPONENTS
	// ─────────────────────────────────────────────────────────
	World.component<FConstraintLink>();
	World.component<FFlecsConstraintData>();

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
				constexpr float DeltaTime = 1.f / 120.f;
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
				constexpr float DeltaTime = 1.f / 120.f;
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
			// Get static data from prefab (via IsA inheritance)
			const FProjectileStatic* ProjStatic = Entity.try_get<FProjectileStatic>();

			constexpr float DeltaTime = 1.f / 120.f;
			ProjInstance.LifetimeRemaining -= DeltaTime;
			if (ProjInstance.LifetimeRemaining <= 0.f)
			{
				UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG Lifetime EXPIRED: Key=%llu FlecsId=%llu"),
					static_cast<uint64>(Body.BarrageKey), Entity.id());
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
						UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG VelocityCheck KILLED: Key=%llu FlecsId=%llu Vel=%.2f"),
							static_cast<uint64>(Body.BarrageKey), Entity.id(), Velocity.Size());
						Entity.add<FTagDead>();
					}
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("PROJ_DEBUG PhysicsBody GONE early: Key=%llu FlecsId=%llu"),
						static_cast<uint64>(Body.BarrageKey), Entity.id());
					Entity.add<FTagDead>();
				}
			}
		});

	// ═══════════════════════════════════════════════════════════════
	// COLLISION PAIR SYSTEMS
	// Process collision pairs created by OnBarrageContact().
	// Order: Damage → Bounce → Pickup → Destructible
	// ═══════════════════════════════════════════════════════════════

	// ─────────────────────────────────────────────────────────
	// DAMAGE COLLISION SYSTEM
	// Uses Static/Instance architecture:
	// - FDamageStatic (prefab): Damage, bAreaDamage, bDestroyOnHit
	// - FHealthStatic (prefab): Armor
	// - FHealthInstance (entity): CurrentHP
	// ─────────────────────────────────────────────────────────
	World.system<const FCollisionPair>("DamageCollisionSystem")
		.with<FTagCollisionDamage>()
		.without<FTagCollisionProcessed>()
		.each([&World](flecs::entity PairEntity, const FCollisionPair& Pair)
		{
			uint64 ProjectileId = Pair.GetProjectileEntityId();
			uint64 TargetId = Pair.GetTargetEntityId();

			float Damage = 25.f; // Default for Artillery projectiles
			bool bAreaDamage = false;
			bool bDestroyOnHit = false;
			int32 MaxBounces = 0;

			flecs::entity ProjectileEntity;

			// Get damage data from projectile
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
						bAreaDamage = DmgStatic->bAreaDamage;
						bDestroyOnHit = DmgStatic->bDestroyOnHit;
					}

					if (ProjStatic)
					{
						MaxBounces = ProjStatic->MaxBounces;
					}
				}
			}

			// Apply damage to target
			if (TargetId != 0)
			{
				flecs::entity Target = World.entity(TargetId);
				if (Target.is_valid() && !Target.has<FTagDead>())
				{
					const FHealthStatic* HealthStatic = Target.try_get<FHealthStatic>();
					FHealthInstance* HealthInstance = Target.try_get_mut<FHealthInstance>();

					if (HealthInstance)
					{
						float Armor = HealthStatic ? HealthStatic->Armor : 0.f;
						float MaxHP = HealthStatic ? HealthStatic->MaxHP : 100.f;

						float EffectiveDamage = FMath::Max(0.f, Damage - Armor);
						HealthInstance->CurrentHP -= EffectiveDamage;

						UE_LOG(LogTemp, Log, TEXT("COLLISION: Damage %.1f applied to Entity %llu (HP: %.1f/%.1f)"),
							EffectiveDamage, TargetId, HealthInstance->CurrentHP, MaxHP);
					}
				}
			}

			// Kill non-bouncing, non-area projectile after hit
			if (ProjectileEntity.is_valid())
			{
				bool bIsBouncing = (MaxBounces == -1);
				if (!bAreaDamage && !bIsBouncing)
				{
					ProjectileEntity.add<FTagDead>();
					UE_LOG(LogTemp, Log, TEXT("COLLISION: Projectile %llu killed after damage hit"), ProjectileId);
				}
			}

			PairEntity.add<FTagCollisionProcessed>();
		});

	// ─────────────────────────────────────────────────────────
	// BOUNCE COLLISION SYSTEM
	// Uses Static/Instance architecture:
	// - FProjectileStatic (prefab): MaxBounces, GracePeriodFrames
	// - FProjectileInstance (entity): BounceCount, GraceFramesRemaining
	// ─────────────────────────────────────────────────────────
	World.system<const FCollisionPair>("BounceCollisionSystem")
		.with<FTagCollisionBounce>()
		.without<FTagCollisionProcessed>()
		.each([&World](flecs::entity PairEntity, const FCollisionPair& Pair)
		{
			auto ProcessBounce = [&World](uint64 EntityId) -> bool
			{
				if (EntityId == 0) return false;

				flecs::entity Entity = World.entity(EntityId);
				if (!Entity.is_valid() || Entity.has<FTagDead>()) return false;

				FProjectileInstance* ProjInstance = Entity.try_get_mut<FProjectileInstance>();
				if (!ProjInstance) return false;

				const FProjectileStatic* ProjStatic = Entity.try_get<FProjectileStatic>();
				const int32 GracePeriodFrames = ProjStatic ? ProjStatic->GracePeriodFrames : 30;
				const int32 MaxBounces = ProjStatic ? ProjStatic->MaxBounces : -1;

				ProjInstance->GraceFramesRemaining = GracePeriodFrames;
				ProjInstance->BounceCount++;

				UE_LOG(LogTemp, Log, TEXT("COLLISION: Bounce %d/%d for Entity %llu"),
					ProjInstance->BounceCount, MaxBounces, EntityId);

				if (MaxBounces >= 0 && ProjInstance->BounceCount > MaxBounces)
				{
					Entity.add<FTagDead>();
					UE_LOG(LogTemp, Log, TEXT("COLLISION: Projectile %llu exceeded max bounces"), EntityId);
				}

				return true;
			};

			if (!ProcessBounce(Pair.EntityId1))
			{
				ProcessBounce(Pair.EntityId2);
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
			const FBarrageBody* Body = Entity.try_get<FBarrageBody>();
			const bool bIsProjectile = Entity.has<FTagProjectile>();

			UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG Cleanup START: FlecsId=%llu IsProj=%d HasBody=%d"),
				Entity.id(), bIsProjectile, Body != nullptr);

			if (Body && Body->IsValid())
			{
				FSkeletonKey Key = Body->BarrageKey;

				UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG Cleanup ISM: Key=%llu FlecsId=%llu"),
					static_cast<uint64>(Key), Entity.id());

				// Remove ISM render instance
				if (CachedBarrageDispatch && CachedBarrageDispatch->GetWorld())
				{
					if (UBarrageRenderManager* Renderer = UBarrageRenderManager::Get(CachedBarrageDispatch->GetWorld()))
					{
						Renderer->RemoveInstance(Key);
					}
				}

				// Handle Barrage physics body cleanup
				if (CachedBarrageDispatch)
				{
					FBLet Prim = CachedBarrageDispatch->GetShapeRef(Key);
					bool bPrimValid = FBarragePrimitive::IsNotNull(Prim);

					UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG Cleanup Physics: Key=%llu PrimValid=%d"),
						static_cast<uint64>(Key), bPrimValid);

					if (bPrimValid)
					{
						Prim->ClearFlecsEntity();
						FBarrageKey BarrageKey = Prim->KeyIntoBarrage;
						CachedBarrageDispatch->SetBodyObjectLayer(BarrageKey, Layers::DEBRIS);
						CachedBarrageDispatch->SuggestTombstone(Prim);

						UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG Physics moved to DEBRIS layer: BarrageKey=%llu"),
							BarrageKey.KeyIntoBarrage);
					}
				}
			}

			Entity.destruct();
			UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG Cleanup DONE: FlecsId=%llu"), Entity.id());
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
