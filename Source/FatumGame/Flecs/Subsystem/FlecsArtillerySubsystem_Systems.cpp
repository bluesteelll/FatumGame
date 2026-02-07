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
	World.component<FTagCollisionProcessed>();

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

			// Log total if multiple hits
			if (TotalDamageApplied > 0.f && Pending.Hits.Num() > 1)
			{
				UE_LOG(LogTemp, Log, TEXT("DAMAGE: Entity %llu total damage this tick: %.1f"),
					Entity.id(), TotalDamageApplied);
			}
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
			auto ProcessBounce = [&World](uint64 EntityId, uint64 OtherId) -> bool
			{
				if (EntityId == 0) return false;

				flecs::entity Entity = World.entity(EntityId);
				if (!Entity.is_valid() || Entity.has<FTagDead>()) return false;

				FProjectileInstance* ProjInstance = Entity.try_get_mut<FProjectileInstance>();
				if (!ProjInstance) return false;

				// Skip bounce during grace period (newly spawned, hasn't left spawn area)
				if (ProjInstance->GraceFramesRemaining > 0) return true;

				// Skip bounce with own owner
				if (ProjInstance->OwnerEntityId != 0
					&& static_cast<uint64>(ProjInstance->OwnerEntityId) == OtherId)
				{
					return true;
				}

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
			// (authoritative: set by game thread from UE actor each Tick)
			FVector MuzzleLocation = FVector::ZeroVector;
			FVector FireDirection = FVector::ForwardVector;

			{
				FVector CharPosD = FVector::ZeroVector;
				FVector UseMuzzleOffset = Static->MuzzleOffset;

				const FAimDirection* AimDir = CharacterEntity.try_get<FAimDirection>();
				if (AimDir)
				{
					if (!AimDir->Direction.IsNearlyZero())
					{
						FireDirection = AimDir->Direction;
					}
					if (!AimDir->MuzzleOffset.IsNearlyZero())
					{
						UseMuzzleOffset = AimDir->MuzzleOffset;
					}
					CharPosD = AimDir->CharacterPosition;
				}

				FQuat AimQuat = FRotationMatrix::MakeFromX(FireDirection).ToQuat();
				FTransform MuzzleTransform(AimQuat, CharPosD);
				MuzzleLocation = MuzzleTransform.TransformPosition(UseMuzzleOffset);

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
			const bool bIsBouncing = ProjProfile->IsBouncing();
			const bool bNeedsDynamic = bIsBouncing || GravityFactor > 0.f;

			float Speed = ProjProfile->DefaultSpeed * Static->ProjectileSpeedMultiplier;
			FVector Velocity = FireDirection * Speed;

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
						bIsBouncing ? ProjRestitution : 0.f, ProjFriction);
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
					RenderSpawn.Location = MuzzleLocation;
					RenderSpawn.Direction = FireDirection;
					RenderSpawn.EntityKey = ProjectileKey;
					PendingProjectileSpawns.Enqueue(RenderSpawn);
				}

				UE_LOG(LogTemp, Log, TEXT("WEAPON: Projectile created Key=%llu Entity=%llu (sim thread)"),
					static_cast<uint64>(ProjectileKey), ProjEntity.id());
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
					if (UFlecsRenderManager* Renderer = UFlecsRenderManager::Get(CachedBarrageDispatch->GetWorld()))
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
