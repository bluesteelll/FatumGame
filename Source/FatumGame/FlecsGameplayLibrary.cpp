// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "FlecsGameplayLibrary.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsComponents.h"
#include "Worlds/FlecsWorld.h"
#include "Systems/BarrageEntitySpawner.h"
#include "Engine/StaticMesh.h"

// ═══════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════

static UFlecsArtillerySubsystem* GetFlecsSubsystem(UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return nullptr;
	return World->GetSubsystem<UFlecsArtillerySubsystem>();
}

static flecs::entity GetFlecsEntityForKey(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey)
{
	if (!Subsystem || !BarrageKey.IsValid()) return flecs::entity();
	UFlecsWorld* FlecsWorld = Subsystem->GetFlecsWorld();
	if (!FlecsWorld) return flecs::entity();

	uint64 EntityId = Subsystem->GetFlecsEntityForBarrageKey(BarrageKey);
	if (EntityId == 0) return flecs::entity();

	return FlecsWorld->World.entity(static_cast<flecs::entity_t>(EntityId));
}

// ═══════════════════════════════════════════════════════════════
// SPAWN (game-thread safe)
// ═══════════════════════════════════════════════════════════════

void UFlecsGameplayLibrary::SpawnWorldItem(
	UObject* WorldContextObject,
	UPrimaryDataAsset* ItemDefinition,
	UStaticMesh* Mesh,
	FVector Location,
	int32 Count,
	float DespawnTime,
	EPhysicsLayer PhysicsLayer)
{
	if (!WorldContextObject || !Mesh) return;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return;

	UFlecsArtillerySubsystem* Subsystem = World->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!Subsystem) return;

	// Create Barrage body + ISM render on game thread
	FBarrageSpawnParams Params;
	Params.Mesh = Mesh;
	Params.WorldTransform = FTransform(Location);
	Params.PhysicsLayer = PhysicsLayer;
	Params.bAutoCollider = true;
	Params.bIsMovable = true;

	FBarrageSpawnResult Result = FBarrageSpawnUtils::SpawnEntity(World, Params);
	if (!Result.bSuccess) return;

	// Enqueue Flecs entity creation to Artillery thread
	FSkeletonKey EntityKey = Result.EntityKey;
	Subsystem->EnqueueCommand([Subsystem, EntityKey, ItemDefinition, Mesh, Count, DespawnTime]()
	{
		UFlecsWorld* FlecsWorld = Subsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		flecs::entity Entity = FlecsWorld->World.entity()
			.set<FItemData>({ ItemDefinition, Count, DespawnTime })
			.set<FBarrageBody>({ EntityKey })
			.set<FISMRender>({ Mesh, FVector::OneVector })
			.add<FTagItem>()
			.add<FTagPickupable>();

		Subsystem->RegisterBarrageEntity(EntityKey, Entity.id());
	});
}

void UFlecsGameplayLibrary::SpawnDestructible(
	UObject* WorldContextObject,
	UStaticMesh* Mesh,
	FVector Location,
	float MaxHP,
	EPhysicsLayer PhysicsLayer)
{
	if (!WorldContextObject || !Mesh) return;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return;

	UFlecsArtillerySubsystem* Subsystem = World->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!Subsystem) return;

	FBarrageSpawnParams Params;
	Params.Mesh = Mesh;
	Params.WorldTransform = FTransform(Location);
	Params.PhysicsLayer = PhysicsLayer;
	Params.bAutoCollider = true;
	Params.bIsMovable = true;
	Params.bDestructible = true;

	FBarrageSpawnResult Result = FBarrageSpawnUtils::SpawnEntity(World, Params);
	if (!Result.bSuccess) return;

	FSkeletonKey EntityKey = Result.EntityKey;
	Subsystem->EnqueueCommand([Subsystem, EntityKey, Mesh, MaxHP]()
	{
		UFlecsWorld* FlecsWorld = Subsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		flecs::entity Entity = FlecsWorld->World.entity()
			.set<FHealthData>({ MaxHP, MaxHP, 0.f })
			.set<FBarrageBody>({ EntityKey })
			.set<FISMRender>({ Mesh, FVector::OneVector })
			.add<FTagDestructible>();

		Subsystem->RegisterBarrageEntity(EntityKey, Entity.id());
	});
}

void UFlecsGameplayLibrary::SpawnLootableDestructible(
	UObject* WorldContextObject,
	UStaticMesh* Mesh,
	FVector Location,
	float MaxHP,
	int32 MinDrops,
	int32 MaxDrops,
	EPhysicsLayer PhysicsLayer)
{
	if (!WorldContextObject || !Mesh) return;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return;

	UFlecsArtillerySubsystem* Subsystem = World->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!Subsystem) return;

	FBarrageSpawnParams Params;
	Params.Mesh = Mesh;
	Params.WorldTransform = FTransform(Location);
	Params.PhysicsLayer = PhysicsLayer;
	Params.bAutoCollider = true;
	Params.bIsMovable = true;
	Params.bDestructible = true;

	FBarrageSpawnResult Result = FBarrageSpawnUtils::SpawnEntity(World, Params);
	if (!Result.bSuccess) return;

	FSkeletonKey EntityKey = Result.EntityKey;
	Subsystem->EnqueueCommand([Subsystem, EntityKey, Mesh, MaxHP, MinDrops, MaxDrops]()
	{
		UFlecsWorld* FlecsWorld = Subsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		flecs::entity Entity = FlecsWorld->World.entity()
			.set<FHealthData>({ MaxHP, MaxHP, 0.f })
			.set<FLootData>({ MinDrops, MaxDrops })
			.set<FBarrageBody>({ EntityKey })
			.set<FISMRender>({ Mesh, FVector::OneVector })
			.add<FTagDestructible>()
			.add<FTagHasLoot>();

		Subsystem->RegisterBarrageEntity(EntityKey, Entity.id());
	});
}

// ═══════════════════════════════════════════════════════════════
// ENTITY LIFECYCLE (game-thread safe)
// ═══════════════════════════════════════════════════════════════

void UFlecsGameplayLibrary::KillEntityByBarrageKey(UObject* WorldContextObject, FSkeletonKey BarrageKey)
{
	UFlecsArtillerySubsystem* Subsystem = GetFlecsSubsystem(WorldContextObject);
	if (!Subsystem || !BarrageKey.IsValid()) return;

	FSkeletonKey CapturedKey = BarrageKey;
	Subsystem->EnqueueCommand([Subsystem, CapturedKey]()
	{
		flecs::entity Entity = GetFlecsEntityForKey(Subsystem, CapturedKey);
		if (Entity.is_valid() && Entity.is_alive())
		{
			Entity.add<FTagDead>();
		}
	});
}

// ═══════════════════════════════════════════════════════════════
// DAMAGE & HEALING (game-thread safe)
// ═══════════════════════════════════════════════════════════════

void UFlecsGameplayLibrary::ApplyDamageByBarrageKey(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Damage)
{
	UFlecsArtillerySubsystem* Subsystem = GetFlecsSubsystem(WorldContextObject);
	if (!Subsystem || !BarrageKey.IsValid() || Damage <= 0.f) return;

	FSkeletonKey CapturedKey = BarrageKey;
	Subsystem->EnqueueCommand([Subsystem, CapturedKey, Damage]()
	{
		ApplyDamage_ArtilleryThread(Subsystem, CapturedKey, Damage);
	});
}

void UFlecsGameplayLibrary::HealEntityByBarrageKey(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Amount)
{
	UFlecsArtillerySubsystem* Subsystem = GetFlecsSubsystem(WorldContextObject);
	if (!Subsystem || !BarrageKey.IsValid() || Amount <= 0.f) return;

	FSkeletonKey CapturedKey = BarrageKey;
	Subsystem->EnqueueCommand([Subsystem, CapturedKey, Amount]()
	{
		Heal_ArtilleryThread(Subsystem, CapturedKey, Amount);
	});
}

// ═══════════════════════════════════════════════════════════════
// ITEM OPERATIONS (game-thread safe)
// ═══════════════════════════════════════════════════════════════

void UFlecsGameplayLibrary::SetItemDespawnTimer(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Timer)
{
	UFlecsArtillerySubsystem* Subsystem = GetFlecsSubsystem(WorldContextObject);
	if (!Subsystem || !BarrageKey.IsValid()) return;

	FSkeletonKey CapturedKey = BarrageKey;
	Subsystem->EnqueueCommand([Subsystem, CapturedKey, Timer]()
	{
		flecs::entity Entity = GetFlecsEntityForKey(Subsystem, CapturedKey);
		if (Entity.is_valid() && Entity.is_alive())
		{
			FItemData* Item = Entity.try_get_mut<FItemData>();
			if (Item)
			{
				Item->DespawnTimer = Timer;
			}
		}
	});
}

// ═══════════════════════════════════════════════════════════════
// ARTILLERY THREAD API (direct Flecs access)
// ═══════════════════════════════════════════════════════════════

bool UFlecsGameplayLibrary::ApplyDamage_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey, float Damage)
{
	flecs::entity Entity = GetFlecsEntityForKey(Subsystem, BarrageKey);
	if (!Entity.is_valid() || !Entity.is_alive()) return false;

	FHealthData* Health = Entity.try_get_mut<FHealthData>();
	if (!Health) return false;

	float ActualDamage = FMath::Max(0.f, Damage - Health->Armor);
	Health->CurrentHP -= ActualDamage;

	if (Health->CurrentHP <= 0.f)
	{
		Health->CurrentHP = 0.f;
		Entity.add<FTagDead>();
		return true;
	}
	return false;
}

void UFlecsGameplayLibrary::Heal_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey, float Amount)
{
	flecs::entity Entity = GetFlecsEntityForKey(Subsystem, BarrageKey);
	if (!Entity.is_valid() || !Entity.is_alive()) return;

	FHealthData* Health = Entity.try_get_mut<FHealthData>();
	if (!Health) return;

	Health->CurrentHP = FMath::Min(Health->CurrentHP + Amount, Health->MaxHP);
}

bool UFlecsGameplayLibrary::IsAlive_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey)
{
	flecs::entity Entity = GetFlecsEntityForKey(Subsystem, BarrageKey);
	if (!Entity.is_valid() || !Entity.is_alive()) return false;

	const FHealthData* Health = Entity.try_get<FHealthData>();
	return Health ? Health->IsAlive() : Entity.is_alive();
}

bool UFlecsGameplayLibrary::GetHealth_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey,
	float& OutCurrentHP, float& OutMaxHP)
{
	flecs::entity Entity = GetFlecsEntityForKey(Subsystem, BarrageKey);
	if (!Entity.is_valid() || !Entity.is_alive()) return false;

	const FHealthData* Health = Entity.try_get<FHealthData>();
	if (!Health) return false;

	OutCurrentHP = Health->CurrentHP;
	OutMaxHP = Health->MaxHP;
	return true;
}
