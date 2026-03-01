// FlecsArtillerySubsystem - Collision Pair Systems
// DamageCollisionSystem, BounceCollisionSystem, PickupCollisionSystem, DestructibleCollisionSystem

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsStaticComponents.h"
#include "FlecsInstanceComponents.h"
#include "FBarragePrimitive.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"
#include "FlecsNiagaraManager.h"

void UFlecsArtillerySubsystem::SetupCollisionSystems()
{
	flecs::world& World = *FlecsWorld;

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
						if (ProjInst && ProjInst->IsOwnedBy(TargetId))
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
				if (ProjInstance->IsOwnedBy(OtherId))
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
}
