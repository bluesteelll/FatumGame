// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "FlecsArtillerySubsystem.h"
#include "ArtilleryDispatch.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FlecsComponents.h"

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

	// Create Flecs systems that run each tick on the Artillery thread.
	SetupFlecsSystems();

	// Subscribe to Barrage collision events (runs on Artillery thread).
	SubscribeToBarrageEvents();

	// Register ourselves with ArtilleryDispatch so the busy worker calls our ArtilleryTick.
	UArtilleryDispatch::SelfPtr->SetFlecsDispatch(this);
	SelfPtr = this;

	UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: Online (direct flecs::world, no plugin tick functions)"));
	return true;
}

void UFlecsArtillerySubsystem::SetupFlecsSystems()
{
	flecs::world& World = *FlecsWorld;

	// Register components with Flecs (using direct flecs API, not UFlecsWorld wrapper)
	World.component<FItemData>();
	World.component<FHealthData>();
	World.component<FDamageSource>();
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

	// ─────────────────────────────────────────────────────────
	// ITEM DESPAWN SYSTEM
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
	// Entities tagged FTagDead with a FBarrageBody get their
	// Barrage key unregistered, then the entity is destroyed.
	// Runs after death check.
	// ─────────────────────────────────────────────────────────
	World.system<>("DeadEntityCleanupSystem")
		.with<FTagDead>()
		.each([this](flecs::entity Entity)
		{
			// Unregister from Barrage key index if this entity has a physics body
			const FBarrageBody* Body = Entity.try_get<FBarrageBody>();
			if (Body && Body->IsValid())
			{
				UnregisterBarrageEntity(Body->BarrageKey);
			}

			// Destroy the Flecs entity
			Entity.destruct();
		});
}

void UFlecsArtillerySubsystem::ArtilleryTick()
{
	if (!FlecsWorld) return;

	// Drain the command queue. All mutations from the game thread are applied here,
	// on the artillery thread, before Flecs systems run.
	TFunction<void()> Command;
	while (CommandQueue.Dequeue(Command))
	{
		Command();
	}

	// Progress the Flecs world. This runs all registered Flecs systems.
	// Artillery busy worker runs at ~120Hz, so each tick is ~8.33ms.
	constexpr double ArtilleryDeltaTime = 1.0 / 120.0;
	FlecsWorld->progress(ArtilleryDeltaTime);
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

	// CRITICAL: Clear our reference in Artillery FIRST, before destroying ourselves.
	// The Artillery busy worker thread holds a raw pointer to us and calls ArtilleryTick().
	// If we don't clear this, the thread will call ArtilleryTick() on a destroyed object.
	if (UArtilleryDispatch* ArtilleryDispatch = UArtilleryDispatch::SelfPtr)
	{
		ArtilleryDispatch->SetFlecsDispatch(nullptr);
		UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem: Cleared FlecsDispatch pointer in Artillery"));
	}

	// Unsubscribe from Barrage events
	if (ContactEventHandle.IsValid())
	{
		if (UBarrageDispatch* Barrage = UBarrageDispatch::SelfPtr)
		{
			Barrage->OnBarrageContactAddedDelegate.Remove(ContactEventHandle);
		}
		ContactEventHandle.Reset();
	}

	SelfPtr = nullptr;
	FlecsWorld.Reset();
	BarrageKeyIndex.Empty();
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

	UBarrageDispatch* Barrage = UBarrageDispatch::SelfPtr;
	if (!Barrage) return;

	// Get physics bodies to extract SkeletonKeys
	FBLet Body1 = Barrage->GetShapeRef(Event.ContactEntity1.ContactKey);
	FBLet Body2 = Barrage->GetShapeRef(Event.ContactEntity2.ContactKey);

	FSkeletonKey Key1 = FBarragePrimitive::IsNotNull(Body1) ? Body1->KeyOutOfBarrage : FSkeletonKey();
	FSkeletonKey Key2 = FBarragePrimitive::IsNotNull(Body2) ? Body2->KeyOutOfBarrage : FSkeletonKey();

	// Skip if both keys are invalid
	if (!Key1.IsValid() && !Key2.IsValid()) return;

	// Get Flecs entities
	uint64 FlecsId1 = Key1.IsValid() ? GetFlecsEntityForBarrageKey(Key1) : 0;
	uint64 FlecsId2 = Key2.IsValid() ? GetFlecsEntityForBarrageKey(Key2) : 0;

	// Skip if neither entity is tracked by Flecs
	if (FlecsId1 == 0 && FlecsId2 == 0) return;

	flecs::world& World = *FlecsWorld;

	// ─────────────────────────────────────────────────────────
	// PROJECTILE DAMAGE HANDLING
	// If one entity is a projectile with FDamageSource and the other has FHealthData,
	// apply damage directly.
	// ─────────────────────────────────────────────────────────
	auto TryApplyProjectileDamage = [&](uint64 ProjectileId, uint64 TargetId, bool bProjectileIsEntity1)
	{
		if (ProjectileId == 0 || TargetId == 0) return false;

		flecs::entity Projectile = World.entity(ProjectileId);
		flecs::entity Target = World.entity(TargetId);

		if (!Projectile.is_valid() || !Target.is_valid()) return false;

		const FDamageSource* Damage = Projectile.try_get<FDamageSource>();
		FHealthData* Health = Target.try_get_mut<FHealthData>();

		if (Damage && Health)
		{
			// Apply damage (respecting armor)
			float EffectiveDamage = FMath::Max(0.f, Damage->Damage - Health->Armor);
			Health->CurrentHP -= EffectiveDamage;

			// Kill projectile after hit (unless it's area damage, handle separately)
			if (!Damage->bAreaDamage)
			{
				Projectile.add<FTagDead>();
			}

			return true;
		}
		return false;
	};

	// Check if entity 1 is a projectile hitting entity 2
	if (Event.ContactEntity1.bIsProjectile && FlecsId1 != 0)
	{
		TryApplyProjectileDamage(FlecsId1, FlecsId2, true);
	}
	// Check if entity 2 is a projectile hitting entity 1
	else if (Event.ContactEntity2.bIsProjectile && FlecsId2 != 0)
	{
		TryApplyProjectileDamage(FlecsId2, FlecsId1, false);
	}

	// ─────────────────────────────────────────────────────────
	// DESTRUCTIBLE HANDLING
	// If one entity has FTagDestructible and was hit by a projectile, tag it dead.
	// ─────────────────────────────────────────────────────────
	auto TryDestroyDestructible = [&](uint64 DestructibleId, bool bHitByProjectile)
	{
		if (DestructibleId == 0 || !bHitByProjectile) return;

		flecs::entity Entity = World.entity(DestructibleId);
		if (!Entity.is_valid()) return;

		if (Entity.has<FTagDestructible>())
		{
			// If no health component, just destroy immediately
			if (!Entity.has<FHealthData>())
			{
				Entity.add<FTagDead>();
			}
			// If has health, damage system will handle death
		}
	};

	// Entity 1 hit by projectile (entity 2)
	if (Event.ContactEntity2.bIsProjectile && FlecsId1 != 0)
	{
		TryDestroyDestructible(FlecsId1, true);
	}
	// Entity 2 hit by projectile (entity 1)
	if (Event.ContactEntity1.bIsProjectile && FlecsId2 != 0)
	{
		TryDestroyDestructible(FlecsId2, true);
	}
}

void UFlecsArtillerySubsystem::EnqueueCommand(TFunction<void()>&& Command)
{
	CommandQueue.Enqueue(MoveTemp(Command));
}

void UFlecsArtillerySubsystem::RegisterBarrageEntity(FSkeletonKey BarrageKey, uint64 FlecsEntityId)
{
	BarrageKeyIndex.Add(BarrageKey.Obj, FlecsEntityId);
}

void UFlecsArtillerySubsystem::UnregisterBarrageEntity(FSkeletonKey BarrageKey)
{
	BarrageKeyIndex.Remove(BarrageKey.Obj);
}

uint64 UFlecsArtillerySubsystem::GetFlecsEntityForBarrageKey(FSkeletonKey BarrageKey) const
{
	const uint64* Found = BarrageKeyIndex.Find(BarrageKey.Obj);
	return Found ? *Found : 0;
}

bool UFlecsArtillerySubsystem::HasFlecsEntityForBarrageKey(FSkeletonKey BarrageKey) const
{
	return BarrageKeyIndex.Contains(BarrageKey.Obj);
}
