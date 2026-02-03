// FlecsArtillerySubsystem - Flecs Systems Setup

#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FlecsComponents.h"
#include "EPhysicsLayer.h"
#include "Systems/BarrageEntitySpawner.h"

void UFlecsArtillerySubsystem::SetupFlecsSystems()
{
	flecs::world& World = *FlecsWorld;

	// ═══════════════════════════════════════════════════════════════
	// COMPONENT REGISTRATION
	// Register all Flecs components (using direct flecs API)
	// ═══════════════════════════════════════════════════════════════

	// Core gameplay components
	World.component<FItemData>();
	World.component<FHealthData>();
	World.component<FDamageSource>();
	World.component<FProjectileData>();
	World.component<FLootData>();
	World.component<FBarrageBody>();
	World.component<FISMRender>();
	World.component<FContainerSlot>();
	World.component<FContainerData>();

	// Entity tags
	World.component<FTagItem>();
	World.component<FTagDestructible>();
	World.component<FTagPickupable>();
	World.component<FTagHasLoot>();
	World.component<FTagDead>();
	World.component<FTagProjectile>();
	World.component<FTagCharacter>();

	// Legacy components
	World.component<FFlecsCollisionEvent>();
	World.component<FConstraintLink>();
	World.component<FFlecsConstraintData>();
	World.component<FTagConstrained>();

	// Advanced Item System components
	World.component<FItemStaticData>();  // Prefab component for shared item data
	World.component<FItemInstance>();
	World.component<FItemUniqueData>();
	World.component<FContainedIn>();
	World.component<FWorldItemData>();
	World.component<FContainerBase>();
	World.component<FContainerGridData>();
	World.component<FContainerSlotsData>();
	World.component<FContainerListData>();
	World.component<FTagDroppedItem>();
	World.component<FTagContainer>();
	World.component<FTagEquipment>();
	World.component<FTagConsumable>();

	// Collision Pair System components
	World.component<FCollisionPair>();
	World.component<FTagCollisionDamage>();
	World.component<FTagCollisionPickup>();
	World.component<FTagCollisionBounce>();
	World.component<FTagCollisionDestructible>();
	World.component<FTagCollisionCharacter>();
	World.component<FTagCollisionProcessed>();

	// ═══════════════════════════════════════════════════════════════
	// GAMEPLAY SYSTEMS
	// ═══════════════════════════════════════════════════════════════

	// ─────────────────────────────────────────────────────────
	// WORLD ITEM DESPAWN SYSTEM
	// World items with FWorldItemData get their DespawnTimer
	// decremented. When it hits 0, the entity is tagged FTagDead.
	// ─────────────────────────────────────────────────────────
	World.system<FWorldItemData>("WorldItemDespawnSystem")
		.with<FTagItem>()
		.without<FTagDead>()
		.each([](flecs::entity Entity, FWorldItemData& ItemData)
		{
			if (ItemData.DespawnTimer > 0.f)
			{
				constexpr float DeltaTime = 1.f / 120.f;
				ItemData.DespawnTimer -= DeltaTime;
				if (ItemData.DespawnTimer <= 0.f)
				{
					Entity.add<FTagDead>();
				}
			}
		});

	// ─────────────────────────────────────────────────────────
	// PICKUP GRACE SYSTEM
	// World items with FTagDroppedItem get their PickupGraceTimer
	// decremented. When it hits 0, the tag is removed.
	// ─────────────────────────────────────────────────────────
	World.system<FWorldItemData>("PickupGraceSystem")
		.with<FTagDroppedItem>()
		.each([](flecs::entity Entity, FWorldItemData& ItemData)
		{
			if (ItemData.PickupGraceTimer > 0.f)
			{
				constexpr float DeltaTime = 1.f / 120.f;
				ItemData.PickupGraceTimer -= DeltaTime;
				if (ItemData.PickupGraceTimer <= 0.f)
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
	// LEGACY ITEM DESPAWN SYSTEM
	// Entities with FItemData and a positive DespawnTimer.
	// ─────────────────────────────────────────────────────────
	World.system<FItemData>("ItemDespawnSystem")
		.each([](flecs::entity Entity, FItemData& Item)
		{
			if (Item.DespawnTimer > 0.f)
			{
				constexpr float DeltaTime = 1.f / 120.f;
				Item.DespawnTimer -= DeltaTime;
				if (Item.DespawnTimer <= 0.f)
				{
					Entity.add<FTagDead>();
				}
			}
		});

	// ─────────────────────────────────────────────────────────
	// PROJECTILE LIFETIME SYSTEM
	// Projectiles with FProjectileData get their lifetime
	// decremented. Velocity check is DISABLED for true bouncing
	// projectiles (MaxBounces == -1).
	// ─────────────────────────────────────────────────────────
	World.system<FProjectileData, const FBarrageBody>("ProjectileLifetimeSystem")
		.with<FTagProjectile>()
		.without<FTagDead>()
		.each([this](flecs::entity Entity, FProjectileData& Projectile, const FBarrageBody& Body)
		{
			constexpr float DeltaTime = 1.f / 120.f;
			Projectile.LifetimeRemaining -= DeltaTime;
			if (Projectile.LifetimeRemaining <= 0.f)
			{
				UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG Lifetime EXPIRED: Key=%llu FlecsId=%llu"),
					static_cast<uint64>(Body.BarrageKey), Entity.id());
				Entity.add<FTagDead>();
				return;
			}

			// TRUE bouncing projectiles never die by velocity check
			if (Projectile.MaxBounces == -1)
			{
				return;
			}

			// Decrement grace period counter
			if (Projectile.GraceFramesRemaining > 0)
			{
				Projectile.GraceFramesRemaining--;
				return;
			}

			// Kill stopped projectiles (velocity < 50 units/sec)
			if (Body.IsValid() && CachedBarrageDispatch)
			{
				FBLet Prim = CachedBarrageDispatch->GetShapeRef(Body.BarrageKey);
				if (FBarragePrimitive::IsNotNull(Prim))
				{
					FVector3f Velocity = FBarragePrimitive::GetVelocity(Prim);
					constexpr float MinVelocitySq = 50.f * 50.f;
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
	// Applies damage from FDamageSource entities to FHealthData targets.
	// ─────────────────────────────────────────────────────────
	World.system<const FCollisionPair>("DamageCollisionSystem")
		.with<FTagCollisionDamage>()
		.without<FTagCollisionProcessed>()
		.each([&World](flecs::entity PairEntity, const FCollisionPair& Pair)
		{
			uint64 ProjectileId = Pair.GetProjectileEntityId();
			uint64 TargetId = Pair.GetTargetEntityId();

			float Damage = 25.f; // Default for Artillery projectiles

			flecs::entity ProjectileEntity;
			const FDamageSource* DamageSource = nullptr;
			const FProjectileData* ProjData = nullptr;

			if (ProjectileId != 0)
			{
				ProjectileEntity = World.entity(ProjectileId);
				if (ProjectileEntity.is_valid())
				{
					DamageSource = ProjectileEntity.try_get<FDamageSource>();
					ProjData = ProjectileEntity.try_get<FProjectileData>();
					if (DamageSource)
					{
						Damage = DamageSource->Damage;
					}
				}
			}

			// Apply damage to target
			if (TargetId != 0)
			{
				flecs::entity Target = World.entity(TargetId);
				if (Target.is_valid() && !Target.has<FTagDead>())
				{
					FHealthData* Health = Target.try_get_mut<FHealthData>();
					if (Health)
					{
						float EffectiveDamage = FMath::Max(0.f, Damage - Health->Armor);
						Health->CurrentHP -= EffectiveDamage;

						UE_LOG(LogTemp, Log, TEXT("COLLISION: Damage %.1f applied to Entity %llu (HP: %.1f/%.1f)"),
							EffectiveDamage, TargetId, Health->CurrentHP, Health->MaxHP);
					}
				}
			}

			// Kill non-bouncing, non-area projectile after hit
			if (ProjectileEntity.is_valid() && DamageSource)
			{
				bool bIsBouncing = ProjData && ProjData->MaxBounces == -1;
				if (!DamageSource->bAreaDamage && !bIsBouncing)
				{
					ProjectileEntity.add<FTagDead>();
					UE_LOG(LogTemp, Log, TEXT("COLLISION: Projectile %llu killed after damage hit"), ProjectileId);
				}
			}

			PairEntity.add<FTagCollisionProcessed>();
		});

	// ─────────────────────────────────────────────────────────
	// BOUNCE COLLISION SYSTEM
	// Resets projectile grace period and increments bounce count.
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

				FProjectileData* Projectile = Entity.try_get_mut<FProjectileData>();
				if (!Projectile) return false;

				Projectile->GraceFramesRemaining = FProjectileData::GracePeriodFrames;
				Projectile->BounceCount++;

				UE_LOG(LogTemp, Log, TEXT("COLLISION: Bounce %d/%d for Entity %llu"),
					Projectile->BounceCount, Projectile->MaxBounces, EntityId);

				if (Projectile->MaxBounces >= 0 && Projectile->BounceCount > Projectile->MaxBounces)
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
				const FWorldItemData* WorldData = Item.try_get<FWorldItemData>();
				if (WorldData && !WorldData->CanBePickedUp())
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
	// Entities with FHealthData that have CurrentHP <= 0.
	// ─────────────────────────────────────────────────────────
	World.system<const FHealthData>("DeathCheckSystem")
		.without<FTagDead>()
		.each([](flecs::entity Entity, const FHealthData& Health)
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
