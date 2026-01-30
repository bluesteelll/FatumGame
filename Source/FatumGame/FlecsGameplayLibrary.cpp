// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "FlecsGameplayLibrary.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsComponents.h"
#include "FlecsProjectileDefinition.h"
#include "Systems/BarrageEntitySpawner.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FBShapeParams.h"
#include "Skeletonize.h"
#include "Engine/StaticMesh.h"
#include <atomic>

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
	// Lock-free O(1) lookup via bidirectional binding (atomic in FBarragePrimitive)
	return Subsystem->GetEntityForBarrageKey(BarrageKey);
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
		flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		flecs::entity Entity = FlecsWorld->entity()
			.set<FItemData>({ ItemDefinition, Count, DespawnTime })
			.set<FISMRender>({ Mesh, FVector::OneVector })
			.add<FTagItem>()
			.add<FTagPickupable>();

		// Bidirectional binding: sets FBarrageBody + atomic in FBarragePrimitive
		Subsystem->BindEntityToBarrage(Entity, EntityKey);
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
		flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		flecs::entity Entity = FlecsWorld->entity()
			.set<FHealthData>({ MaxHP, MaxHP, 0.f })
			.set<FISMRender>({ Mesh, FVector::OneVector })
			.add<FTagDestructible>();

		// Bidirectional binding: sets FBarrageBody + atomic in FBarragePrimitive
		Subsystem->BindEntityToBarrage(Entity, EntityKey);
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
		flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		flecs::entity Entity = FlecsWorld->entity()
			.set<FHealthData>({ MaxHP, MaxHP, 0.f })
			.set<FLootData>({ MinDrops, MaxDrops })
			.set<FISMRender>({ Mesh, FVector::OneVector })
			.add<FTagDestructible>()
			.add<FTagHasLoot>();

		// Bidirectional binding: sets FBarrageBody + atomic in FBarragePrimitive
		Subsystem->BindEntityToBarrage(Entity, EntityKey);
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

// ═══════════════════════════════════════════════════════════════
// PROJECTILE SPAWNING
// ═══════════════════════════════════════════════════════════════

namespace
{
	std::atomic<uint32> GProjectileCounter{0};
}

FSkeletonKey UFlecsGameplayLibrary::SpawnProjectileFromDefinition(
	UObject* WorldContextObject,
	UFlecsProjectileDefinition* Definition,
	FVector Location,
	FVector Direction,
	float SpeedOverride)
{
	if (!Definition || !Definition->Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpawnProjectileFromDefinition: Invalid definition or no mesh!"));
		return FSkeletonKey();
	}

	float Speed = SpeedOverride > 0.f ? SpeedOverride : Definition->DefaultSpeed;

	return SpawnProjectile(
		WorldContextObject,
		Definition->Mesh,
		Location,
		Direction,
		Speed,
		Definition->Damage,
		Definition->GravityFactor,
		Definition->LifetimeSeconds,
		Definition->CollisionRadius,
		Definition->VisualScale,
		Definition->bIsBouncing,
		Definition->Restitution,
		Definition->Friction,
		Definition->MaxBounces
	);
}

// ═══════════════════════════════════════════════════════════════
// QUERY (game-thread) - CROSS-THREAD READ WARNING
//
// These functions read Flecs data from Game thread while Artillery
// thread (~120Hz) may be modifying it. This is a KNOWN RACE CONDITION
// with the following mitigations:
//
// 1. Data is POD (float, bool) - atomic on most architectures
// 2. Worst case: stale data by 1-2 frames (8-16ms) - acceptable for UI
// 3. Entity validity checked before access
// 4. Subsystem deinitialize check prevents UAF
//
// For CRITICAL health checks (damage, death), use the _ArtilleryThread
// variants via EnqueueCommand() instead.
//
// Future improvement: Add thread-safe health cache updated each tick.
// ═══════════════════════════════════════════════════════════════

float UFlecsGameplayLibrary::GetEntityHealth(UObject* WorldContextObject, FSkeletonKey BarrageKey)
{
	UFlecsArtillerySubsystem* Subsystem = GetFlecsSubsystem(WorldContextObject);
	if (!Subsystem || !BarrageKey.IsValid()) return -1.f;

	// Safety: Check if subsystem is deinitializing (Flecs world may be destroyed)
	if (!Subsystem->GetFlecsWorld()) return -1.f;

	float CurrentHP, MaxHP;
	if (GetHealth_ArtilleryThread(Subsystem, BarrageKey, CurrentHP, MaxHP))
	{
		return CurrentHP;
	}
	return -1.f;
}

float UFlecsGameplayLibrary::GetEntityMaxHealth(UObject* WorldContextObject, FSkeletonKey BarrageKey)
{
	UFlecsArtillerySubsystem* Subsystem = GetFlecsSubsystem(WorldContextObject);
	if (!Subsystem || !BarrageKey.IsValid()) return -1.f;

	// Safety: Check if subsystem is deinitializing (Flecs world may be destroyed)
	if (!Subsystem->GetFlecsWorld()) return -1.f;

	float CurrentHP, MaxHP;
	if (GetHealth_ArtilleryThread(Subsystem, BarrageKey, CurrentHP, MaxHP))
	{
		return MaxHP;
	}
	return -1.f;
}

bool UFlecsGameplayLibrary::IsEntityAlive(UObject* WorldContextObject, FSkeletonKey BarrageKey)
{
	UFlecsArtillerySubsystem* Subsystem = GetFlecsSubsystem(WorldContextObject);
	if (!Subsystem || !BarrageKey.IsValid()) return false;

	// Safety: Check if subsystem is deinitializing (Flecs world may be destroyed)
	if (!Subsystem->GetFlecsWorld()) return false;

	return IsAlive_ArtilleryThread(Subsystem, BarrageKey);
}

// ═══════════════════════════════════════════════════════════════
// PROJECTILE SPAWNING
// ═══════════════════════════════════════════════════════════════

FSkeletonKey UFlecsGameplayLibrary::SpawnProjectile(
	UObject* WorldContextObject,
	UStaticMesh* Mesh,
	FVector Location,
	FVector Direction,
	float Speed,
	float Damage,
	float GravityFactor,
	float LifetimeSeconds,
	float CollisionRadius,
	float VisualScale,
	bool bIsBouncing,
	float Restitution,
	float Friction,
	int32 MaxBounces)
{
	if (!WorldContextObject || !Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("PROJ_DEBUG SpawnProjectile: Invalid parameters! Mesh=%p"), Mesh);
		return FSkeletonKey();
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("PROJ_DEBUG SpawnProjectile: No World!"));
		return FSkeletonKey();
	}

	UBarrageDispatch* Barrage = World->GetSubsystem<UBarrageDispatch>();
	UBarrageRenderManager* Renderer = UBarrageRenderManager::Get(World);
	UFlecsArtillerySubsystem* Subsystem = World->GetSubsystem<UFlecsArtillerySubsystem>();

	if (!Barrage)
	{
		UE_LOG(LogTemp, Error, TEXT("PROJ_DEBUG SpawnProjectile: No UBarrageDispatch!"));
		return FSkeletonKey();
	}

	// Generate unique key
	const uint32 Id = ++GProjectileCounter;
	FSkeletonKey EntityKey = FSkeletonKey(FORGE_SKELETON_KEY(Id, SKELLY::SFIX_GUN_SHOT));

	UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG SpawnProjectile START: Key=%llu Counter=%u Loc=%s Bouncing=%d"),
		static_cast<uint64>(EntityKey), Id, *Location.ToString(), bIsBouncing);

	// Normalize direction
	Direction.Normalize();
	FVector Velocity = Direction * Speed;

	// Create sphere physics body
	FBSphereParams SphereParams = FBarrageBounder::GenerateSphereBounds(Location, CollisionRadius);

	FBLet Body;
	if (bIsBouncing)
	{
		// Bouncing projectile - uses PROJECTILE layer for proper collision detection
		Body = Barrage->CreateBouncingSphere(
			SphereParams,
			EntityKey,
			static_cast<uint16>(EPhysicsLayer::PROJECTILE),
			Restitution,
			Friction
		);
		UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG CreateBouncingSphere: Key=%llu Body=%p"),
			static_cast<uint64>(EntityKey), Body.Get());
	}
	else
	{
		// Sensor projectile - destroyed on first hit
		Body = Barrage->CreatePrimitive(
			SphereParams,
			EntityKey,
			static_cast<uint16>(EPhysicsLayer::PROJECTILE),
			true // IsSensor
		);
		UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG CreatePrimitive: Key=%llu Body=%p"),
			static_cast<uint64>(EntityKey), Body.Get());
	}

	if (!FBarragePrimitive::IsNotNull(Body))
	{
		UE_LOG(LogTemp, Error, TEXT("PROJ_DEBUG SpawnProjectile: FAILED to create physics body! Key=%llu"),
			static_cast<uint64>(EntityKey));
		return FSkeletonKey();
	}

	// Set velocity and gravity
	FBarragePrimitive::SetVelocity(Velocity, Body);
	FBarragePrimitive::SetGravityFactor(GravityFactor, Body);

	// Add ISM render instance
	if (Renderer)
	{
		FTransform RenderTransform;
		RenderTransform.SetLocation(Location);
		RenderTransform.SetScale3D(FVector(VisualScale));
		int32 ISMIndex = Renderer->AddInstance(Mesh, nullptr, RenderTransform, EntityKey);
		UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG AddInstance: Key=%llu ISMIndex=%d"),
			static_cast<uint64>(EntityKey), ISMIndex);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("PROJ_DEBUG SpawnProjectile: No Renderer! Key=%llu"),
			static_cast<uint64>(EntityKey));
	}

	// Create Flecs entity on Artillery thread
	if (Subsystem)
	{
		Subsystem->EnqueueCommand([Subsystem, EntityKey, Mesh, Damage, LifetimeSeconds, VisualScale, MaxBounces]()
		{
			flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
			if (!FlecsWorld)
			{
				UE_LOG(LogTemp, Error, TEXT("PROJ_DEBUG FlecsEntity: No FlecsWorld! Key=%llu"),
					static_cast<uint64>(EntityKey));
				return;
			}

			flecs::entity Entity = FlecsWorld->entity()
				.set<FDamageSource>({ Damage, FGameplayTag(), false, 0.f })
				.set<FProjectileData>({ LifetimeSeconds, MaxBounces, 0 })
				.set<FISMRender>({ Mesh, FVector(VisualScale) })
				.add<FTagProjectile>();

			// Bidirectional binding: sets FBarrageBody + atomic in FBarragePrimitive
			Subsystem->BindEntityToBarrage(Entity, EntityKey);
			UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG FlecsEntity CREATED: Key=%llu FlecsId=%llu"),
				static_cast<uint64>(EntityKey), Entity.id());
		});
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("PROJ_DEBUG SpawnProjectile: No FlecsSubsystem! Key=%llu"),
			static_cast<uint64>(EntityKey));
	}

	UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG SpawnProjectile DONE: Key=%llu Vel=%s"),
		static_cast<uint64>(EntityKey), *Velocity.ToString());

	return EntityKey;
}
