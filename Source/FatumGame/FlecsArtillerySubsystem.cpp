// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "FlecsArtillerySubsystem.h"
#include "ArtilleryDispatch.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FlecsComponents.h"
#include "FlecsItemDefinition.h"
#include "FlecsEntityDefinition.h"
#include "ItemRegistry.h"
#include "Systems/BarrageEntitySpawner.h"
#include "EPhysicsLayer.h"

bool UFlecsArtillerySubsystem::RegistrationImplementation()
{
	// Create our own flecs::world directly - no plugin involvement, no tick functions, no worker threads.
	// This world runs ONLY on the Artillery thread via ArtilleryTick().
	FlecsWorld = MakeUnique<flecs::world>();

	if (!FlecsWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("FlecsArtillerySubsystem: Failed to create Flecs world. Flecs ECS will not run."));
		return false;
	}

	// CRITICAL: Disable Flecs' internal threading - Artillery thread is our only executor.
	// Setting threads to 0 means all systems run synchronously in progress().
	FlecsWorld->set_threads(0);

	// Cache subsystem pointers to avoid repeated SelfPtr lookups on hot paths.
	CachedBarrageDispatch = UBarrageDispatch::SelfPtr;
	CachedArtilleryDispatch = UArtilleryDispatch::SelfPtr;

	if (!CachedBarrageDispatch)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: UBarrageDispatch not available during registration"));
	}

	// Create Flecs systems that run each tick on the Artillery thread.
	SetupFlecsSystems();

	// Subscribe to Barrage collision events (runs on Artillery thread).
	SubscribeToBarrageEvents();

	// Register ourselves with ArtilleryDispatch so the busy worker calls our ArtilleryTick.
	if (CachedArtilleryDispatch)
	{
		CachedArtilleryDispatch->SetFlecsDispatch(this);
	}
	SelfPtr = this;

	UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: Online (lock-free bidirectional binding, %d stages available)"), MaxStages);
	return true;
}

void UFlecsArtillerySubsystem::SetupFlecsSystems()
{
	flecs::world& World = *FlecsWorld;

	// Register components with Flecs (using direct flecs API, not UFlecsWorld wrapper)
	World.component<FItemData>();
	World.component<FHealthData>();
	World.component<FDamageSource>();
	World.component<FProjectileData>();
	World.component<FLootData>();
	World.component<FBarrageBody>();
	World.component<FISMRender>();
	World.component<FContainerSlot>();
	World.component<FContainerData>();
	World.component<FTagItem>();
	World.component<FTagDestructible>();
	World.component<FTagPickupable>();
	World.component<FTagHasLoot>();
	World.component<FTagDead>();
	World.component<FTagProjectile>();
	World.component<FTagCharacter>();
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

	// ─────────────────────────────────────────────────────────
	// WORLD ITEM DESPAWN SYSTEM (NEW)
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
	// PICKUP GRACE SYSTEM (NEW)
	// World items with FTagDroppedItem get their PickupGraceTimer
	// decremented. When it hits 0, the tag is removed and item
	// becomes pickupable.
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
	// Entities with FItemData and a positive DespawnTimer get
	// their timer decremented. When it hits 0, the entity is
	// tagged FTagDead for cleanup.
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
	// decremented. When it hits 0, the projectile is destroyed.
	// Velocity check is DISABLED for true bouncing projectiles
	// (MaxBounces == -1) - they only die by lifetime.
	//
	// NOTE: Captures 'this' to use CachedBarrageDispatch instead of
	// unsafe UBarrageDispatch::SelfPtr access. The pointer is cached
	// at registration time and cleared during Deinitialize().
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

			// TRUE bouncing projectiles (MaxBounces == -1) never die by velocity check
			// They only die by lifetime or MaxBounces limit (if set)
			if (Projectile.MaxBounces == -1)
			{
				return;
			}

			// For non-bouncing or limited-bounce projectiles:
			// Decrement grace period counter
			if (Projectile.GraceFramesRemaining > 0)
			{
				Projectile.GraceFramesRemaining--;
				return; // Skip velocity check during grace period
			}

			// Kill stopped projectiles (velocity < 50 units/sec)
			// Use CachedBarrageDispatch (safe) instead of UBarrageDispatch::SelfPtr (race condition)
			if (Body.IsValid() && CachedBarrageDispatch)
			{
				FBLet Prim = CachedBarrageDispatch->GetShapeRef(Body.BarrageKey);
				if (FBarragePrimitive::IsNotNull(Prim))
				{
					FVector3f Velocity = FBarragePrimitive::GetVelocity(Prim);
					constexpr float MinVelocitySq = 50.f * 50.f; // 50 units/sec minimum
					if (Velocity.SizeSquared() < MinVelocitySq)
					{
						UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG VelocityCheck KILLED: Key=%llu FlecsId=%llu Vel=%.2f"),
							static_cast<uint64>(Body.BarrageKey), Entity.id(), Velocity.Size());
						Entity.add<FTagDead>();
					}
				}
				else
				{
					// Physics body already gone! Mark dead to cleanup ISM.
					UE_LOG(LogTemp, Warning, TEXT("PROJ_DEBUG PhysicsBody GONE early: Key=%llu FlecsId=%llu"),
						static_cast<uint64>(Body.BarrageKey), Entity.id());
					Entity.add<FTagDead>();
				}
			}
		});

	// ─────────────────────────────────────────────────────────
	// DEATH CHECK SYSTEM
	// Entities with FHealthData that have CurrentHP <= 0 are
	// tagged FTagDead.
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
	// 2. Unbind from Barrage (clears atomic in FBarragePrimitive)
	// 3. Move physics body to DEBRIS layer (disables gameplay collision)
	// 4. Mark for deferred destruction via tombstone
	// 5. Destroy Flecs entity
	//
	// NOTE: DO NOT call FinalizeReleasePrimitive() - causes crash on PIE exit!
	// Instead, move to DEBRIS layer for immediate collision disable,
	// and let tombstone handle safe deferred destruction.
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

				// Remove ISM render instance (scheduled to game thread via render manager tick)
				if (CachedBarrageDispatch && CachedBarrageDispatch->GetWorld())
				{
					if (UBarrageRenderManager* Renderer = UBarrageRenderManager::Get(CachedBarrageDispatch->GetWorld()))
					{
						Renderer->RemoveInstance(Key);
					}
				}

				// Handle Barrage physics body cleanup:
				// 1. Move to DEBRIS layer immediately - disables collision with gameplay entities
				// 2. Tombstone marks it for safe deferred destruction (~19 sec delay)
				// This approach is crash-safe: no immediate Jolt body destruction.
				if (CachedBarrageDispatch)
				{
					FBLet Prim = CachedBarrageDispatch->GetShapeRef(Key);
					bool bPrimValid = FBarragePrimitive::IsNotNull(Prim);

					UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG Cleanup Physics: Key=%llu PrimValid=%d"),
						static_cast<uint64>(Key), bPrimValid);

					if (bPrimValid)
					{
						// Clear Flecs binding in FBarragePrimitive (reverse direction)
						Prim->ClearFlecsEntity();

						// Get the internal Jolt key (FBarrageKey)
						FBarrageKey BarrageKey = Prim->KeyIntoBarrage;

						// Move to DEBRIS layer - immediately disables collision with:
						// MOVING, PROJECTILE, HITBOX, ENEMY, ENEMYPROJECTILE, etc.
						// Only collides with NON_MOVING (static geometry) - harmless
						CachedBarrageDispatch->SetBodyObjectLayer(BarrageKey, Layers::DEBRIS);

						// Tombstone for safe deferred destruction
						CachedBarrageDispatch->SuggestTombstone(Prim);

						UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG Physics moved to DEBRIS layer: BarrageKey=%llu"),
							BarrageKey.KeyIntoBarrage);
					}
				}
			}

			// Destroy the Flecs entity (Flecs handles component cleanup automatically)
			Entity.destruct();

			UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG Cleanup DONE: FlecsId=%llu"), Entity.id());
		});
}

void UFlecsArtillerySubsystem::ArtilleryTick()
{
	// CRITICAL: Check if deinitializing. If so, exit immediately without touching FlecsWorld.
	// This prevents use-after-free when Game thread is destroying us.
	if (bDeinitializing.load(std::memory_order_acquire))
	{
		return;
	}

	// Mark that we're inside ArtilleryTick. Deinitialize() will wait for this to become false.
	bInArtilleryTick.store(true, std::memory_order_release);

	// Double-check after setting flag (handles race where Deinitialize started between checks)
	if (bDeinitializing.load(std::memory_order_acquire) || !FlecsWorld)
	{
		bInArtilleryTick.store(false, std::memory_order_release);
		return;
	}

	// Drain the command queue. All mutations from the game thread are applied here,
	// on the artillery thread, before Flecs systems run.
	TFunction<void()> Command;
	while (CommandQueue.Dequeue(Command))
	{
		// Check for deinit between commands (early exit if shutting down)
		if (bDeinitializing.load(std::memory_order_acquire))
		{
			bInArtilleryTick.store(false, std::memory_order_release);
			return;
		}
		Command();
	}

	// Final check before expensive progress() call
	if (bDeinitializing.load(std::memory_order_acquire))
	{
		bInArtilleryTick.store(false, std::memory_order_release);
		return;
	}

	// Progress the Flecs world. This runs all registered Flecs systems.
	// Artillery busy worker runs at ~120Hz, so each tick is ~8.33ms.
	constexpr double ArtilleryDeltaTime = 1.0 / 120.0;
	FlecsWorld->progress(ArtilleryDeltaTime);

	// Clear the in-tick flag
	bInArtilleryTick.store(false, std::memory_order_release);
}

void UFlecsArtillerySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Declare dependencies BEFORE Super::Initialize() for correct deinitialization order.
	// FlecsArtillerySubsystem MUST deinitialize BEFORE Artillery (which calls our ArtilleryTick).
	// Artillery MUST deinitialize BEFORE Barrage (which we subscribe to for collision events).
	Collection.InitializeDependency<UArtilleryDispatch>();
	Collection.InitializeDependency<UBarrageDispatch>();

	Super::Initialize(Collection);
	SET_INITIALIZATION_ORDER_BY_ORDINATEKEY_AND_WORLD
}

void UFlecsArtillerySubsystem::PostInitialize()
{
	Super::PostInitialize();
}

void UFlecsArtillerySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
}

void UFlecsArtillerySubsystem::Deinitialize()
{
	UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: Deinitializing..."));

	// ═══════════════════════════════════════════════════════════════
	// STEP 1: Signal ArtilleryTick() to stop and exit early.
	// This MUST happen BEFORE clearing FlecsDispatch pointer.
	// ═══════════════════════════════════════════════════════════════
	bDeinitializing.store(true, std::memory_order_release);

	// ═══════════════════════════════════════════════════════════════
	// STEP 2: Clear our reference in Artillery to prevent NEW calls.
	// The Artillery busy worker checks this before calling ArtilleryTick().
	// ═══════════════════════════════════════════════════════════════
	if (CachedArtilleryDispatch)
	{
		CachedArtilleryDispatch->SetFlecsDispatch(nullptr);
		UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: Cleared FlecsDispatch pointer in Artillery"));
	}

	// ═══════════════════════════════════════════════════════════════
	// STEP 3: Wait for any in-flight ArtilleryTick() to complete.
	// This is the synchronization barrier that prevents use-after-free.
	// Timeout after ~100ms (12 Artillery ticks at 120Hz) to prevent deadlock.
	// ═══════════════════════════════════════════════════════════════
	constexpr int32 MaxSpinIterations = 1000; // ~100ms with 0.1ms sleep
	int32 SpinCount = 0;
	while (bInArtilleryTick.load(std::memory_order_acquire))
	{
		if (++SpinCount > MaxSpinIterations)
		{
			UE_LOG(LogTemp, Error, TEXT("FlecsArtillerySubsystem: Timeout waiting for ArtilleryTick to exit! Potential crash risk."));
			break;
		}
		FPlatformProcess::Sleep(0.0001f); // 0.1ms
	}

	if (SpinCount > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: Waited %d iterations for ArtilleryTick to exit"), SpinCount);
	}

	// ═══════════════════════════════════════════════════════════════
	// STEP 4: Safe cleanup - ArtilleryTick is guaranteed to not be running.
	// ═══════════════════════════════════════════════════════════════

	// Unsubscribe from Barrage events
	if (ContactEventHandle.IsValid())
	{
		if (CachedBarrageDispatch)
		{
			CachedBarrageDispatch->OnBarrageContactAddedDelegate.Remove(ContactEventHandle);
		}
		ContactEventHandle.Reset();
	}

	// Clear cached pointers
	CachedBarrageDispatch = nullptr;
	CachedArtilleryDispatch = nullptr;

	SelfPtr = nullptr;

	// NOW safe to destroy FlecsWorld - Artillery thread is not accessing it
	FlecsWorld.Reset();

	Super::Deinitialize();
}

void UFlecsArtillerySubsystem::SubscribeToBarrageEvents()
{
	UBarrageDispatch* Barrage = UBarrageDispatch::SelfPtr;
	if (!Barrage)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: UBarrageDispatch not found, collision events disabled."));
		return;
	}

	ContactEventHandle = Barrage->OnBarrageContactAddedDelegate.AddUObject(
		this, &UFlecsArtillerySubsystem::OnBarrageContact
	);

	UE_LOG(LogTemp, Log, TEXT("FlecsArtillerySubsystem: Subscribed to Barrage collision events."));
}

void UFlecsArtillerySubsystem::OnBarrageContact(const BarrageContactEvent& Event)
{
	if (!FlecsWorld) return;
	if (!CachedBarrageDispatch) return;

	// Get physics bodies - O(1) lookup via libcuckoo
	FBLet Body1 = CachedBarrageDispatch->GetShapeRef(Event.ContactEntity1.ContactKey);
	FBLet Body2 = CachedBarrageDispatch->GetShapeRef(Event.ContactEntity2.ContactKey);

	// Extract SkeletonKeys (forward binding: FBarragePrimitive → SkeletonKey)
	FSkeletonKey Key1 = FBarragePrimitive::IsNotNull(Body1) ? Body1->KeyOutOfBarrage : FSkeletonKey();
	FSkeletonKey Key2 = FBarragePrimitive::IsNotNull(Body2) ? Body2->KeyOutOfBarrage : FSkeletonKey();

	// Skip if both keys are invalid
	if (!Key1.IsValid() && !Key2.IsValid()) return;

	// Get Flecs entities via LOCK-FREE atomic read from FBarragePrimitive - O(1)
	// Returns 0 if entity is not tracked by Flecs (e.g. Artillery projectiles)
	uint64 FlecsId1 = FBarragePrimitive::IsNotNull(Body1) ? Body1->GetFlecsEntity() : 0;
	uint64 FlecsId2 = FBarragePrimitive::IsNotNull(Body2) ? Body2->GetFlecsEntity() : 0;

	flecs::world& World = *FlecsWorld;

	// ─────────────────────────────────────────────────────────
	// ARTILLERY PROJECTILE DAMAGE HANDLING
	// Artillery projectiles (spawned via UArtilleryLibrary) are NOT in Flecs.
	// We detect them via bIsProjectile flag and apply default damage to Flecs targets.
	// ─────────────────────────────────────────────────────────
	constexpr float DefaultProjectileDamage = 25.f;

	// Helper: Apply damage from ANY projectile (Artillery or Flecs) to a Flecs target
	auto TryApplyDamageToFlecsTarget = [&](uint64 TargetFlecsId, uint64 ProjectileFlecsId, float Damage) -> bool
	{
		if (TargetFlecsId == 0) return false;

		flecs::entity Target = World.entity(TargetFlecsId);
		if (!Target.is_valid()) return false;

		FHealthData* Health = Target.try_get_mut<FHealthData>();
		if (Health)
		{
			// If projectile IS in Flecs, use its FDamageSource
			if (ProjectileFlecsId != 0)
			{
				flecs::entity Projectile = World.entity(ProjectileFlecsId);
				if (Projectile.is_valid())
				{
					const FDamageSource* DamageSource = Projectile.try_get<FDamageSource>();
					if (DamageSource)
					{
						Damage = DamageSource->Damage;

						// Kill projectile after hit ONLY if it's not a bouncing projectile
						// Bouncing projectiles (MaxBounces == -1) should continue bouncing
						const FProjectileData* ProjData = Projectile.try_get<FProjectileData>();
						bool bIsBouncing = ProjData && ProjData->MaxBounces == -1;

						if (!DamageSource->bAreaDamage && !bIsBouncing)
						{
							const FBarrageBody* ProjBody = Projectile.try_get<FBarrageBody>();
							UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG HitEntity KILLED: Key=%llu FlecsId=%llu IsArea=%d IsBouncing=%d"),
								ProjBody ? static_cast<uint64>(ProjBody->BarrageKey) : 0, ProjectileFlecsId, DamageSource->bAreaDamage, bIsBouncing);
							Projectile.add<FTagDead>();
						}
					}
				}
			}

			// Apply damage (respecting armor)
			float EffectiveDamage = FMath::Max(0.f, Damage - Health->Armor);
			Health->CurrentHP -= EffectiveDamage;

			UE_LOG(LogTemp, Log, TEXT("FlecsCollision: Projectile hit! Damage=%.1f, HP=%.1f/%.1f"),
				EffectiveDamage, Health->CurrentHP, Health->MaxHP);
			return true;
		}

		// No health? Check if destructible
		if (Target.has<FTagDestructible>())
		{
			Target.add<FTagDead>();
			UE_LOG(LogTemp, Log, TEXT("FlecsCollision: Destructible destroyed by projectile"));
			return true;
		}

		return false;
	};

	// Entity 1 is projectile, Entity 2 is target
	if (Event.ContactEntity1.bIsProjectile && FlecsId2 != 0)
	{
		TryApplyDamageToFlecsTarget(FlecsId2, FlecsId1, DefaultProjectileDamage);
	}
	// Entity 2 is projectile, Entity 1 is target
	else if (Event.ContactEntity2.bIsProjectile && FlecsId1 != 0)
	{
		TryApplyDamageToFlecsTarget(FlecsId1, FlecsId2, DefaultProjectileDamage);
	}

	// ─────────────────────────────────────────────────────────
	// BOUNCE GRACE PERIOD RESET
	// When a Flecs projectile collides with anything (wall, floor, etc.),
	// reset its grace period so velocity check doesn't kill it mid-bounce.
	// ─────────────────────────────────────────────────────────
	auto ResetGracePeriodIfProjectile = [&](uint64 FlecsId)
	{
		if (FlecsId == 0) return;
		flecs::entity Entity = World.entity(FlecsId);
		if (!Entity.is_valid()) return;

		FProjectileData* Projectile = Entity.try_get_mut<FProjectileData>();
		if (Projectile)
		{
			// Reset grace period on any collision (bounce)
			Projectile->GraceFramesRemaining = FProjectileData::GracePeriodFrames;
			Projectile->BounceCount++;

			const FBarrageBody* Body = Entity.try_get<FBarrageBody>();
			FSkeletonKey Key = Body ? Body->BarrageKey : FSkeletonKey();

			UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG Bounce: Key=%llu FlecsId=%llu Count=%d/%d"),
				static_cast<uint64>(Key), FlecsId, Projectile->BounceCount, Projectile->MaxBounces);

			// Check max bounces limit
			if (Projectile->MaxBounces >= 0 && Projectile->BounceCount > Projectile->MaxBounces)
			{
				UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG MaxBounces EXCEEDED: Key=%llu FlecsId=%llu"),
					static_cast<uint64>(Key), FlecsId);
				Entity.add<FTagDead>();
			}
		}
	};

	// Reset grace period for any Flecs projectile that collided
	if (FlecsId1 != 0)
	{
		flecs::entity E1 = World.entity(FlecsId1);
		if (E1.is_valid() && E1.has<FTagProjectile>())
		{
			ResetGracePeriodIfProjectile(FlecsId1);
		}
	}
	if (FlecsId2 != 0)
	{
		flecs::entity E2 = World.entity(FlecsId2);
		if (E2.is_valid() && E2.has<FTagProjectile>())
		{
			ResetGracePeriodIfProjectile(FlecsId2);
		}
	}

	// ─────────────────────────────────────────────────────────
	// FLECS-TO-FLECS COLLISION (both entities in Flecs)
	// Handle cases like Flecs projectile hitting Flecs target
	// ─────────────────────────────────────────────────────────
	if (FlecsId1 != 0 && FlecsId2 != 0)
	{
		flecs::entity Entity1 = World.entity(FlecsId1);
		flecs::entity Entity2 = World.entity(FlecsId2);

		if (Entity1.is_valid() && Entity2.is_valid())
		{
			// Check if Entity1 has FDamageSource (it's a Flecs projectile)
			const FDamageSource* Damage1 = Entity1.try_get<FDamageSource>();
			if (Damage1)
			{
				FHealthData* Health2 = Entity2.try_get_mut<FHealthData>();
				if (Health2)
				{
					float EffectiveDamage = FMath::Max(0.f, Damage1->Damage - Health2->Armor);
					Health2->CurrentHP -= EffectiveDamage;
					if (!Damage1->bAreaDamage) Entity1.add<FTagDead>();
				}
			}

			// Check if Entity2 has FDamageSource (it's a Flecs projectile)
			const FDamageSource* Damage2 = Entity2.try_get<FDamageSource>();
			if (Damage2)
			{
				FHealthData* Health1 = Entity1.try_get_mut<FHealthData>();
				if (Health1)
				{
					float EffectiveDamage = FMath::Max(0.f, Damage2->Damage - Health1->Armor);
					Health1->CurrentHP -= EffectiveDamage;
					if (!Damage2->bAreaDamage) Entity2.add<FTagDead>();
				}
			}
		}
	}
}

void UFlecsArtillerySubsystem::EnqueueCommand(TFunction<void()>&& Command)
{
	CommandQueue.Enqueue(MoveTemp(Command));
}

// ═══════════════════════════════════════════════════════════════
// BIDIRECTIONAL BINDING API (Lock-Free)
// ═══════════════════════════════════════════════════════════════

flecs::world UFlecsArtillerySubsystem::GetStage(int32 ThreadIndex) const
{
	check(FlecsWorld);
	// For now, always return main world (stage 0).
	// Future: create and return thread-specific stages for parallel collision processing.
	// Stages buffer commands and merge atomically during world.progress().
	return FlecsWorld->get_stage(FMath::Clamp(ThreadIndex, 0, MaxStages - 1));
}

void UFlecsArtillerySubsystem::BindEntityToBarrage(flecs::entity Entity, FSkeletonKey BarrageKey)
{
	if (!Entity.is_valid() || !BarrageKey.IsValid()) return;

	// Forward binding: set FBarrageBody component on Flecs entity
	Entity.set<FBarrageBody>({ BarrageKey });

	// Reverse binding: store Flecs entity ID in FBarragePrimitive (atomic)
	if (CachedBarrageDispatch)
	{
		FBLet Prim = CachedBarrageDispatch->GetShapeRef(BarrageKey);
		if (FBarragePrimitive::IsNotNull(Prim))
		{
			Prim->SetFlecsEntity(Entity.id());
		}
	}
}

void UFlecsArtillerySubsystem::UnbindEntityFromBarrage(flecs::entity Entity)
{
	if (!Entity.is_valid()) return;

	// Get BarrageKey from forward binding before removing it
	const FBarrageBody* Body = Entity.try_get<FBarrageBody>();
	if (Body && Body->IsValid())
	{
		FSkeletonKey Key = Body->BarrageKey;

		// Clear reverse binding (atomic in FBarragePrimitive)
		if (CachedBarrageDispatch)
		{
			FBLet Prim = CachedBarrageDispatch->GetShapeRef(Key);
			if (FBarragePrimitive::IsNotNull(Prim))
			{
				Prim->ClearFlecsEntity();
			}
		}
	}

	// Clear forward binding by removing component
	Entity.remove<FBarrageBody>();
}

flecs::entity UFlecsArtillerySubsystem::GetEntityForBarrageKey(FSkeletonKey BarrageKey) const
{
	if (!FlecsWorld || !BarrageKey.IsValid()) return flecs::entity();

	// Lock-free O(1) lookup: libcuckoo → FBLet → atomic load
	if (CachedBarrageDispatch)
	{
		FBLet Prim = CachedBarrageDispatch->GetShapeRef(BarrageKey);
		if (FBarragePrimitive::IsNotNull(Prim))
		{
			uint64 FlecsId = Prim->GetFlecsEntity();
			if (FlecsId != 0)
			{
				return FlecsWorld->entity(FlecsId);
			}
		}
	}
	return flecs::entity();
}

FSkeletonKey UFlecsArtillerySubsystem::GetBarrageKeyForEntity(flecs::entity Entity) const
{
	if (!Entity.is_valid()) return FSkeletonKey();

	// O(1) lookup via Flecs sparse set
	const FBarrageBody* Body = Entity.try_get<FBarrageBody>();
	return Body ? Body->BarrageKey : FSkeletonKey();
}

bool UFlecsArtillerySubsystem::HasEntityForBarrageKey(FSkeletonKey BarrageKey) const
{
	if (!BarrageKey.IsValid()) return false;

	// Lock-free O(1) check: libcuckoo → FBLet → atomic load
	if (CachedBarrageDispatch)
	{
		FBLet Prim = CachedBarrageDispatch->GetShapeRef(BarrageKey);
		if (FBarragePrimitive::IsNotNull(Prim))
		{
			return Prim->HasFlecsEntity();
		}
	}
	return false;
}

// ═══════════════════════════════════════════════════════════════
// DEPRECATED API (backward compatibility during migration)
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::RegisterBarrageEntity(FSkeletonKey BarrageKey, uint64 FlecsEntityId)
{
	// Deprecated: Use BindEntityToBarrage instead.
	// This only sets the reverse binding - callers should already have set FBarrageBody.
	if (CachedBarrageDispatch && BarrageKey.IsValid())
	{
		FBLet Prim = CachedBarrageDispatch->GetShapeRef(BarrageKey);
		if (FBarragePrimitive::IsNotNull(Prim))
		{
			Prim->SetFlecsEntity(FlecsEntityId);
		}
	}
}

void UFlecsArtillerySubsystem::UnregisterBarrageEntity(FSkeletonKey BarrageKey)
{
	// Deprecated: Use UnbindEntityFromBarrage instead.
	// This only clears the reverse binding.
	if (CachedBarrageDispatch && BarrageKey.IsValid())
	{
		FBLet Prim = CachedBarrageDispatch->GetShapeRef(BarrageKey);
		if (FBarragePrimitive::IsNotNull(Prim))
		{
			Prim->ClearFlecsEntity();
		}
	}
}

uint64 UFlecsArtillerySubsystem::GetFlecsEntityForBarrageKey(FSkeletonKey BarrageKey) const
{
	// Deprecated: Use GetEntityForBarrageKey instead.
	flecs::entity Entity = GetEntityForBarrageKey(BarrageKey);
	return Entity.is_valid() ? Entity.id() : 0;
}

bool UFlecsArtillerySubsystem::HasFlecsEntityForBarrageKey(FSkeletonKey BarrageKey) const
{
	// Deprecated: Use HasEntityForBarrageKey instead.
	return HasEntityForBarrageKey(BarrageKey);
}

UBarrageRenderManager* UFlecsArtillerySubsystem::GetRenderManager() const
{
	if (CachedBarrageDispatch && CachedBarrageDispatch->GetWorld())
	{
		return UBarrageRenderManager::Get(CachedBarrageDispatch->GetWorld());
	}
	return nullptr;
}

// ═══════════════════════════════════════════════════════════════
// ITEM PREFAB REGISTRY IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════

flecs::entity UFlecsArtillerySubsystem::GetOrCreateItemPrefab(UFlecsEntityDefinition* EntityDefinition)
{
	if (!FlecsWorld || !EntityDefinition)
	{
		return flecs::entity();
	}

	// EntityDefinition must have ItemDefinition profile
	UFlecsItemDefinition* ItemDef = EntityDefinition->ItemDefinition;
	if (!ItemDef)
	{
		UE_LOG(LogTemp, Warning, TEXT("GetOrCreateItemPrefab: EntityDefinition '%s' has no ItemDefinition"),
			*EntityDefinition->GetName());
		return flecs::entity();
	}

	int32 TypeId = ItemDef->ItemTypeId;
	if (TypeId == 0)
	{
		// Auto-generate TypeId from name if not set
		TypeId = GetTypeHash(ItemDef->ItemName);
	}

	// Check if prefab already exists
	if (flecs::entity* Existing = ItemPrefabs.Find(TypeId))
	{
		if (Existing->is_valid() && Existing->is_alive())
		{
			return *Existing;
		}
	}

	// Create new prefab
	FString PrefabName = FString::Printf(TEXT("ItemPrefab_%s"), *ItemDef->ItemName.ToString());
	flecs::entity Prefab = FlecsWorld->prefab(TCHAR_TO_ANSI(*PrefabName));

	// Set static data on prefab - inherited by all instances via is_a()
	FItemStaticData StaticData;
	StaticData.TypeId = TypeId;
	StaticData.MaxStack = ItemDef->MaxStackSize;
	StaticData.Weight = ItemDef->Weight;
	StaticData.GridSize = ItemDef->GridSize;
	StaticData.ItemName = ItemDef->ItemName;
	StaticData.EntityDefinition = EntityDefinition;
	StaticData.ItemDefinition = ItemDef;
	Prefab.set<FItemStaticData>(StaticData);

	// Store in registry
	ItemPrefabs.Add(TypeId, Prefab);

	UE_LOG(LogTemp, Log, TEXT("Created item prefab: '%s' (TypeId=%d, MaxStack=%d)"),
		*ItemDef->ItemName.ToString(), TypeId, ItemDef->MaxStackSize);

	return Prefab;
}

flecs::entity UFlecsArtillerySubsystem::GetItemPrefab(int32 TypeId) const
{
	if (const flecs::entity* Found = ItemPrefabs.Find(TypeId))
	{
		return *Found;
	}
	return flecs::entity();
}

UFlecsEntityDefinition* UFlecsArtillerySubsystem::GetEntityDefinitionForItem(flecs::entity ItemEntity) const
{
	if (!ItemEntity.is_valid())
	{
		return nullptr;
	}

	// FItemStaticData is inherited from prefab via is_a()
	// try_get<>() returns pointer, automatically resolves through IsA relationship
	const FItemStaticData* StaticData = ItemEntity.try_get<FItemStaticData>();
	if (StaticData)
	{
		return StaticData->EntityDefinition;
	}

	return nullptr;
}

UFlecsItemDefinition* UFlecsArtillerySubsystem::GetItemDefinitionForItem(flecs::entity ItemEntity) const
{
	if (!ItemEntity.is_valid())
	{
		return nullptr;
	}

	// FItemStaticData is inherited from prefab via is_a()
	const FItemStaticData* StaticData = ItemEntity.try_get<FItemStaticData>();
	if (StaticData)
	{
		return StaticData->ItemDefinition;
	}

	return nullptr;
}
