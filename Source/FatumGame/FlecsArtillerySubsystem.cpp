// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "FlecsArtillerySubsystem.h"
#include "ArtilleryDispatch.h"
#include "FlecsComponents.h"
#include "Worlds/FlecsWorld.h"
#include "Worlds/FlecsWorldSubsystem.h"

bool UFlecsArtillerySubsystem::RegistrationImplementation()
{
	// Get the Flecs world from the plugin's subsystem.
	// By this point in the ordinate sequence, all subsystems are initialized.
	UFlecsWorldSubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsWorldSubsystem>();
	if (FlecsSubsystem && FlecsSubsystem->HasValidFlecsWorld())
	{
		FlecsWorld = FlecsSubsystem->GetDefaultWorld();
	}

	if (!FlecsWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("FlecsArtillerySubsystem: Could not obtain Flecs world. Flecs ECS will not run."));
		return false;
	}

	// Create Flecs systems that run each tick on the Artillery thread.
	SetupFlecsSystems();

	// Register ourselves with ArtilleryDispatch so the busy worker calls our ArtilleryTick.
	UArtilleryDispatch::SelfPtr->SetFlecsDispatch(this);
	SelfPtr = this;

	UE_LOG(LogTemp, Warning, TEXT("FlecsArtillerySubsystem:Subsystem: Online"));
	return true;
}

void UFlecsArtillerySubsystem::SetupFlecsSystems()
{
	flecs::world& World = FlecsWorld->World;

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
	FlecsWorld->World.progress(ArtilleryDeltaTime);
}

void UFlecsArtillerySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	SET_INITIALIZATION_ORDER_BY_ORDINATEKEY_AND_WORLD
	Super::Initialize(Collection);
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
	SelfPtr = nullptr;
	FlecsWorld = nullptr;
	BarrageKeyIndex.Empty();
	Super::Deinitialize();
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
