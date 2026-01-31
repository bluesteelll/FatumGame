// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "FlecsGameplayLibrary.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsComponents.h"
#include "FlecsProjectileDefinition.h"
#include "FlecsConstrainedGroupDefinition.h"
#include "Systems/BarrageEntitySpawner.h"
#include "BarrageDispatch.h"
#include "BarrageConstraintSystem.h"
#include "FWorldSimOwner.h"
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

// ═══════════════════════════════════════════════════════════════
// CONSTRAINTS (game-thread safe, synchronous access to constraint system)
// Note: Constraint operations go through Barrage/Jolt which is thread-safe.
// We update Flecs FFlecsConstraintData via EnqueueCommand.
// ═══════════════════════════════════════════════════════════════

static UBarrageDispatch* GetBarrageDispatch(UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return nullptr;
	return World->GetSubsystem<UBarrageDispatch>();
}

int64 UFlecsGameplayLibrary::CreateFixedConstraint(
	UObject* WorldContextObject,
	FSkeletonKey Entity1Key,
	FSkeletonKey Entity2Key,
	float BreakForce,
	float BreakTorque)
{
	UBarrageDispatch* Barrage = GetBarrageDispatch(WorldContextObject);
	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!Barrage || !FlecsSubsystem) return 0;

	// Get BarrageKeys from SkeletonKeys
	FBarrageKey Body1 = Barrage->GetBarrageKeyFromSkeletonKey(Entity1Key);
	FBarrageKey Body2 = Barrage->GetBarrageKeyFromSkeletonKey(Entity2Key);

	if (Body1.KeyIntoBarrage == 0 || Body2.KeyIntoBarrage == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("CreateFixedConstraint: Invalid body keys! Entity1=%llu Entity2=%llu"),
			static_cast<uint64>(Entity1Key), static_cast<uint64>(Entity2Key));
		return 0;
	}

	// Create constraint in Barrage (thread-safe)
	FBarrageConstraintKey ConstraintKey = Barrage->CreateFixedConstraint(Body1, Body2, BreakForce, BreakTorque);
	if (!ConstraintKey.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("CreateFixedConstraint: Failed to create constraint!"));
		return 0;
	}

	// Update FFlecsConstraintData on both Flecs entities (via Artillery thread)
	int64 KeyValue = ConstraintKey.Key;
	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Entity1Key, Entity2Key, KeyValue, BreakForce, BreakTorque]()
	{
		flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		// Update Entity1
		flecs::entity E1 = FlecsSubsystem->GetEntityForBarrageKey(Entity1Key);
		if (E1.is_valid() && E1.is_alive())
		{
			if (!E1.has<FFlecsConstraintData>())
			{
				E1.set<FFlecsConstraintData>({});
			}
			FFlecsConstraintData* Data = E1.try_get_mut<FFlecsConstraintData>();
			if (Data)
			{
				Data->AddConstraint(KeyValue, Entity2Key, BreakForce, BreakTorque);
			}
			E1.add<FTagConstrained>();
		}

		// Update Entity2
		flecs::entity E2 = FlecsSubsystem->GetEntityForBarrageKey(Entity2Key);
		if (E2.is_valid() && E2.is_alive())
		{
			if (!E2.has<FFlecsConstraintData>())
			{
				E2.set<FFlecsConstraintData>({});
			}
			FFlecsConstraintData* Data = E2.try_get_mut<FFlecsConstraintData>();
			if (Data)
			{
				Data->AddConstraint(KeyValue, Entity1Key, BreakForce, BreakTorque);
			}
			E2.add<FTagConstrained>();
		}
	});

	UE_LOG(LogTemp, Log, TEXT("CreateFixedConstraint: Created constraint %lld between %llu and %llu"),
		KeyValue, static_cast<uint64>(Entity1Key), static_cast<uint64>(Entity2Key));

	return KeyValue;
}

int64 UFlecsGameplayLibrary::CreateHingeConstraint(
	UObject* WorldContextObject,
	FSkeletonKey Entity1Key,
	FSkeletonKey Entity2Key,
	FVector WorldAnchor,
	FVector HingeAxis,
	float BreakForce)
{
	UBarrageDispatch* Barrage = GetBarrageDispatch(WorldContextObject);
	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!Barrage || !FlecsSubsystem) return 0;

	FBarrageKey Body1 = Barrage->GetBarrageKeyFromSkeletonKey(Entity1Key);
	FBarrageKey Body2 = Barrage->GetBarrageKeyFromSkeletonKey(Entity2Key);

	if (Body1.KeyIntoBarrage == 0 || Body2.KeyIntoBarrage == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("CreateHingeConstraint: Invalid body keys!"));
		return 0;
	}

	FBarrageConstraintKey ConstraintKey = Barrage->CreateHingeConstraint(Body1, Body2, WorldAnchor, HingeAxis, BreakForce);
	if (!ConstraintKey.IsValid())
	{
		return 0;
	}

	int64 KeyValue = ConstraintKey.Key;
	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Entity1Key, Entity2Key, KeyValue, BreakForce]()
	{
		flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		auto UpdateEntity = [&](flecs::entity E, FSkeletonKey OtherKey)
		{
			if (E.is_valid() && E.is_alive())
			{
				if (!E.has<FFlecsConstraintData>())
				{
					E.set<FFlecsConstraintData>({});
				}
				FFlecsConstraintData* Data = E.try_get_mut<FFlecsConstraintData>();
				if (Data)
				{
					Data->AddConstraint(KeyValue, OtherKey, BreakForce, 0.f);
				}
				E.add<FTagConstrained>();
			}
		};

		UpdateEntity(FlecsSubsystem->GetEntityForBarrageKey(Entity1Key), Entity2Key);
		UpdateEntity(FlecsSubsystem->GetEntityForBarrageKey(Entity2Key), Entity1Key);
	});

	return KeyValue;
}

int64 UFlecsGameplayLibrary::CreateDistanceConstraint(
	UObject* WorldContextObject,
	FSkeletonKey Entity1Key,
	FSkeletonKey Entity2Key,
	float MinDistance,
	float MaxDistance,
	float BreakForce,
	float SpringFrequency,
	float SpringDamping,
	bool bLockRotation)
{
	UBarrageDispatch* Barrage = GetBarrageDispatch(WorldContextObject);
	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!Barrage || !FlecsSubsystem) return 0;

	FBarrageKey Body1 = Barrage->GetBarrageKeyFromSkeletonKey(Entity1Key);
	FBarrageKey Body2 = Barrage->GetBarrageKeyFromSkeletonKey(Entity2Key);

	if (Body1.KeyIntoBarrage == 0 || Body2.KeyIntoBarrage == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("CreateDistanceConstraint: Invalid body keys!"));
		return 0;
	}

	UE_LOG(LogTemp, Warning, TEXT("GameplayLib CreateDistance: MinDist=%.1f, MaxDist=%.1f, SpringFreq=%.2f, SpringDamp=%.2f, LockRot=%d"),
		MinDistance, MaxDistance, SpringFrequency, SpringDamping, bLockRotation ? 1 : 0);

	FBarrageConstraintKey ConstraintKey = Barrage->CreateDistanceConstraint(Body1, Body2, MinDistance, MaxDistance, BreakForce, SpringFrequency, SpringDamping, bLockRotation);
	if (!ConstraintKey.IsValid())
	{
		return 0;
	}

	int64 KeyValue = ConstraintKey.Key;
	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Entity1Key, Entity2Key, KeyValue, BreakForce]()
	{
		flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		auto UpdateEntity = [&](flecs::entity E, FSkeletonKey OtherKey)
		{
			if (E.is_valid() && E.is_alive())
			{
				if (!E.has<FFlecsConstraintData>())
				{
					E.set<FFlecsConstraintData>({});
				}
				FFlecsConstraintData* Data = E.try_get_mut<FFlecsConstraintData>();
				if (Data)
				{
					Data->AddConstraint(KeyValue, OtherKey, BreakForce, 0.f);
				}
				E.add<FTagConstrained>();
			}
		};

		UpdateEntity(FlecsSubsystem->GetEntityForBarrageKey(Entity1Key), Entity2Key);
		UpdateEntity(FlecsSubsystem->GetEntityForBarrageKey(Entity2Key), Entity1Key);
	});

	return KeyValue;
}

int64 UFlecsGameplayLibrary::CreatePointConstraint(
	UObject* WorldContextObject,
	FSkeletonKey Entity1Key,
	FSkeletonKey Entity2Key,
	float BreakForce,
	float BreakTorque)
{
	UBarrageDispatch* Barrage = GetBarrageDispatch(WorldContextObject);
	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!Barrage || !FlecsSubsystem) return 0;

	FBarrageKey Body1 = Barrage->GetBarrageKeyFromSkeletonKey(Entity1Key);
	FBarrageKey Body2 = Barrage->GetBarrageKeyFromSkeletonKey(Entity2Key);

	if (Body1.KeyIntoBarrage == 0 || Body2.KeyIntoBarrage == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("CreatePointConstraint: Invalid body keys!"));
		return 0;
	}

	FBarrageConstraintSystem* ConstraintSystem = Barrage->GetConstraintSystem();
	if (!ConstraintSystem) return 0;

	FBPointConstraintParams Params;
	Params.Body1 = Body1;
	Params.Body2 = Body2;
	Params.Space = EBConstraintSpace::WorldSpace;
	Params.bAutoDetectAnchor = true;
	Params.BreakForce = BreakForce;
	Params.BreakTorque = BreakTorque;

	FBarrageConstraintKey ConstraintKey = ConstraintSystem->CreatePoint(Params);
	if (!ConstraintKey.IsValid())
	{
		return 0;
	}

	int64 KeyValue = ConstraintKey.Key;
	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Entity1Key, Entity2Key, KeyValue, BreakForce, BreakTorque]()
	{
		auto UpdateEntity = [&](flecs::entity E, FSkeletonKey OtherKey)
		{
			if (E.is_valid() && E.is_alive())
			{
				if (!E.has<FFlecsConstraintData>())
				{
					E.set<FFlecsConstraintData>({});
				}
				FFlecsConstraintData* Data = E.try_get_mut<FFlecsConstraintData>();
				if (Data)
				{
					Data->AddConstraint(KeyValue, OtherKey, BreakForce, BreakTorque);
				}
				E.add<FTagConstrained>();
			}
		};

		UpdateEntity(FlecsSubsystem->GetEntityForBarrageKey(Entity1Key), Entity2Key);
		UpdateEntity(FlecsSubsystem->GetEntityForBarrageKey(Entity2Key), Entity1Key);
	});

	return KeyValue;
}

bool UFlecsGameplayLibrary::RemoveConstraint(UObject* WorldContextObject, int64 ConstraintKey)
{
	UBarrageDispatch* Barrage = GetBarrageDispatch(WorldContextObject);
	if (!Barrage || ConstraintKey == 0) return false;

	FBarrageConstraintKey Key;
	Key.Key = ConstraintKey;

	bool bRemoved = Barrage->RemoveConstraint(Key);

	// Note: FFlecsConstraintData cleanup on entities happens automatically when
	// constraint breaking is processed via ProcessBreakableConstraints()
	// or can be cleaned up manually if needed.

	return bRemoved;
}

int32 UFlecsGameplayLibrary::RemoveAllConstraintsFromEntity(UObject* WorldContextObject, FSkeletonKey EntityKey)
{
	UBarrageDispatch* Barrage = GetBarrageDispatch(WorldContextObject);
	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!Barrage || !FlecsSubsystem || !EntityKey.IsValid()) return 0;

	FBarrageKey BodyKey = Barrage->GetBarrageKeyFromSkeletonKey(EntityKey);
	if (BodyKey.KeyIntoBarrage == 0) return 0;

	FBarrageConstraintSystem* ConstraintSystem = Barrage->GetConstraintSystem();
	if (!ConstraintSystem) return 0;

	int32 RemovedCount = ConstraintSystem->RemoveAllForBody(BodyKey);

	// Clear FFlecsConstraintData on entity
	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, EntityKey]()
	{
		flecs::entity Entity = FlecsSubsystem->GetEntityForBarrageKey(EntityKey);
		if (Entity.is_valid() && Entity.is_alive())
		{
			Entity.remove<FFlecsConstraintData>();
			Entity.remove<FTagConstrained>();
		}
	});

	return RemovedCount;
}

bool UFlecsGameplayLibrary::IsConstraintActive(UObject* WorldContextObject, int64 ConstraintKey)
{
	UBarrageDispatch* Barrage = GetBarrageDispatch(WorldContextObject);
	if (!Barrage || ConstraintKey == 0) return false;

	FBarrageConstraintSystem* ConstraintSystem = Barrage->GetConstraintSystem();
	if (!ConstraintSystem) return false;

	FBarrageConstraintKey Key;
	Key.Key = ConstraintKey;

	return ConstraintSystem->IsValid(Key);
}

bool UFlecsGameplayLibrary::GetConstraintStressRatio(UObject* WorldContextObject, int64 ConstraintKey, float& OutStressRatio)
{
	OutStressRatio = 0.f;

	UBarrageDispatch* Barrage = GetBarrageDispatch(WorldContextObject);
	if (!Barrage || ConstraintKey == 0) return false;

	FBarrageConstraintSystem* ConstraintSystem = Barrage->GetConstraintSystem();
	if (!ConstraintSystem) return false;

	FBarrageConstraintKey Key;
	Key.Key = ConstraintKey;

	FBConstraintForces Forces;
	if (!ConstraintSystem->GetForces(Key, Forces))
	{
		return false;
	}

	// We need to access break thresholds to compute ratio
	// For now, return the raw force magnitude - UI can normalize if needed
	// TODO: Add GetBreakThresholds() to FBarrageConstraintSystem if needed
	OutStressRatio = Forces.GetForceMagnitude();
	return true;
}

// ═══════════════════════════════════════════════════════════════
// CONSTRAINED GROUP SPAWNING
// ═══════════════════════════════════════════════════════════════

namespace
{
	std::atomic<uint32> GGroupElementCounter{0};
}

FFlecsGroupSpawnResult UFlecsGameplayLibrary::SpawnConstrainedGroup(
	UObject* WorldContextObject,
	UFlecsConstrainedGroupDefinition* Definition,
	FVector Location,
	FRotator Rotation)
{
	FFlecsGroupSpawnResult Result;
	Result.bSuccess = false;

	if (!WorldContextObject || !Definition || !Definition->IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("SpawnConstrainedGroup: Invalid parameters!"));
		return Result;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return Result;

	UBarrageDispatch* Barrage = World->GetSubsystem<UBarrageDispatch>();
	UBarrageRenderManager* Renderer = UBarrageRenderManager::Get(World);
	UFlecsArtillerySubsystem* FlecsSubsystem = World->GetSubsystem<UFlecsArtillerySubsystem>();

	if (!Barrage || !FlecsSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("SpawnConstrainedGroup: Missing subsystems!"));
		return Result;
	}

	FTransform GroupTransform(Rotation, Location);
	Result.ElementKeys.Reserve(Definition->Elements.Num());
	Result.ConstraintKeys.Reserve(Definition->Constraints.Num());

	// ═══════════════════════════════════════════════════════════════
	// PHASE 1: Spawn all elements (Barrage bodies + ISM)
	// ═══════════════════════════════════════════════════════════════

	for (const FFlecsGroupElement& Element : Definition->Elements)
	{
		if (!Element.Mesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("SpawnConstrainedGroup: Element '%s' has no mesh!"), *Element.ElementName.ToString());
			Result.ElementKeys.Add(FSkeletonKey());
			continue;
		}

		// Calculate world transform
		FTransform LocalTransform(Element.LocalRotation, Element.LocalOffset, Element.Scale);
		FTransform WorldTransform = LocalTransform * GroupTransform;

		// Generate unique key
		const uint32 Id = ++GGroupElementCounter;
		FSkeletonKey EntityKey = FSkeletonKey(FORGE_SKELETON_KEY(Id, SKELLY::SFIX_ITEM));

		// Create physics body
		FBarrageSpawnParams Params;
		Params.Mesh = Element.Mesh;
		Params.WorldTransform = WorldTransform;
		Params.EntityKey = EntityKey;
		Params.PhysicsLayer = Element.PhysicsLayer;
		Params.bAutoCollider = true;
		Params.bIsMovable = Element.bIsMovable;
		Params.bDestructible = Element.MaxHealth > 0.f;
		Params.Friction = Element.Friction;
		Params.Restitution = Element.Restitution;

		FBarrageSpawnResult SpawnResult = FBarrageSpawnUtils::SpawnEntity(World, Params);
		if (!SpawnResult.bSuccess)
		{
			UE_LOG(LogTemp, Warning, TEXT("SpawnConstrainedGroup: Failed to spawn element '%s'"), *Element.ElementName.ToString());
			Result.ElementKeys.Add(FSkeletonKey());
			continue;
		}

		// Add ISM render instance
		if (Renderer)
		{
			Renderer->AddInstance(Element.Mesh, Element.Material, WorldTransform, EntityKey);
		}

		Result.ElementKeys.Add(EntityKey);

		// Queue Flecs entity creation
		float MaxHP = Element.MaxHealth;
		float ArmorVal = Element.Armor;
		UStaticMesh* MeshPtr = Element.Mesh;
		FVector ScaleVal = Element.Scale;

		FlecsSubsystem->EnqueueCommand([FlecsSubsystem, EntityKey, MeshPtr, ScaleVal, MaxHP, ArmorVal]()
		{
			flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
			if (!FlecsWorld) return;

			flecs::entity Entity = FlecsWorld->entity()
				.set<FISMRender>({ MeshPtr, ScaleVal });

			if (MaxHP > 0.f)
			{
				Entity.set<FHealthData>({ MaxHP, MaxHP, ArmorVal });
				Entity.add<FTagDestructible>();
			}

			FlecsSubsystem->BindEntityToBarrage(Entity, EntityKey);
		});
	}

	// ═══════════════════════════════════════════════════════════════
	// PHASE 2: Create constraints between elements
	// ═══════════════════════════════════════════════════════════════

	for (const FFlecsGroupConstraint& ConstraintDef : Definition->Constraints)
	{
		// Validate indices
		if (ConstraintDef.Element1Index >= Result.ElementKeys.Num() ||
			ConstraintDef.Element2Index >= Result.ElementKeys.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("SpawnConstrainedGroup: Invalid constraint indices!"));
			Result.ConstraintKeys.Add(0);
			continue;
		}

		FSkeletonKey Key1 = Result.ElementKeys[ConstraintDef.Element1Index];
		FSkeletonKey Key2 = Result.ElementKeys[ConstraintDef.Element2Index];

		if (!Key1.IsValid() || !Key2.IsValid())
		{
			Result.ConstraintKeys.Add(0);
			continue;
		}

		FBarrageKey Body1 = Barrage->GetBarrageKeyFromSkeletonKey(Key1);
		FBarrageKey Body2 = Barrage->GetBarrageKeyFromSkeletonKey(Key2);

		if (Body1.KeyIntoBarrage == 0 || Body2.KeyIntoBarrage == 0)
		{
			Result.ConstraintKeys.Add(0);
			continue;
		}

		FBarrageConstraintKey ConstraintKey;

		// Calculate world anchor for hinge/distance
		const FFlecsGroupElement& Elem1 = Definition->Elements[ConstraintDef.Element1Index];
		FTransform LocalTransform1(Elem1.LocalRotation, Elem1.LocalOffset, Elem1.Scale);
		FTransform WorldTransform1 = LocalTransform1 * GroupTransform;
		FVector WorldAnchor = WorldTransform1.TransformPosition(ConstraintDef.AnchorOffset1);
		FVector WorldHingeAxis = WorldTransform1.TransformVectorNoScale(ConstraintDef.HingeAxis);

		switch (ConstraintDef.ConstraintType)
		{
		case EFlecsConstraintType::Fixed:
			ConstraintKey = Barrage->CreateFixedConstraint(Body1, Body2, ConstraintDef.BreakForce, ConstraintDef.BreakTorque);
			break;

		case EFlecsConstraintType::Hinge:
			ConstraintKey = Barrage->CreateHingeConstraint(Body1, Body2, WorldAnchor, WorldHingeAxis, ConstraintDef.BreakForce);
			break;

		case EFlecsConstraintType::Distance:
			ConstraintKey = Barrage->CreateDistanceConstraint(Body1, Body2, ConstraintDef.MinDistance, ConstraintDef.MaxDistance, ConstraintDef.BreakForce, ConstraintDef.SpringFrequency, ConstraintDef.SpringDamping, ConstraintDef.bLockRotation);
			break;

		case EFlecsConstraintType::Point:
			{
				// Point constraint uses the same system as fixed but with different settings
				FBarrageConstraintSystem* System = Barrage->GetConstraintSystem();
				if (System)
				{
					FBPointConstraintParams Params;
					Params.Body1 = Body1;
					Params.Body2 = Body2;
					Params.Space = EBConstraintSpace::WorldSpace;
					Params.AnchorPoint1 = WorldAnchor;
					Params.AnchorPoint2 = WorldTransform1.TransformPosition(ConstraintDef.AnchorOffset2);
					Params.BreakForce = ConstraintDef.BreakForce;
					Params.BreakTorque = ConstraintDef.BreakTorque;
					ConstraintKey = System->CreatePoint(Params);
				}
			}
			break;
		}

		int64 KeyValue = ConstraintKey.Key;
		Result.ConstraintKeys.Add(KeyValue);

		// Update Flecs constraint data
		if (ConstraintKey.IsValid())
		{
			float BreakF = ConstraintDef.BreakForce;
			float BreakT = ConstraintDef.BreakTorque;

			FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Key1, Key2, KeyValue, BreakF, BreakT]()
			{
				auto UpdateEntity = [&](flecs::entity E, FSkeletonKey OtherKey)
				{
					if (E.is_valid() && E.is_alive())
					{
						if (!E.has<FFlecsConstraintData>())
						{
							E.set<FFlecsConstraintData>({});
						}
						FFlecsConstraintData* Data = E.try_get_mut<FFlecsConstraintData>();
						if (Data)
						{
							Data->AddConstraint(KeyValue, OtherKey, BreakF, BreakT);
						}
						E.add<FTagConstrained>();
					}
				};

				UpdateEntity(FlecsSubsystem->GetEntityForBarrageKey(Key1), Key2);
				UpdateEntity(FlecsSubsystem->GetEntityForBarrageKey(Key2), Key1);
			});
		}
	}

	Result.bSuccess = true;
	UE_LOG(LogTemp, Log, TEXT("SpawnConstrainedGroup: Spawned %d elements with %d constraints"),
		Result.ElementKeys.Num(), Result.ConstraintKeys.Num());

	return Result;
}

FFlecsGroupSpawnResult UFlecsGameplayLibrary::SpawnChain(
	UObject* WorldContextObject,
	UStaticMesh* Mesh,
	FVector StartLocation,
	FVector Direction,
	int32 Count,
	float Spacing,
	float BreakForce,
	float MaxHealth)
{
	FFlecsGroupSpawnResult Result;
	Result.bSuccess = false;

	if (!WorldContextObject || !Mesh || Count < 1)
	{
		return Result;
	}

	Direction.Normalize();
	FVector SpacingVec = Direction * Spacing;

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return Result;

	UBarrageDispatch* Barrage = World->GetSubsystem<UBarrageDispatch>();
	UBarrageRenderManager* Renderer = UBarrageRenderManager::Get(World);
	UFlecsArtillerySubsystem* FlecsSubsystem = World->GetSubsystem<UFlecsArtillerySubsystem>();

	if (!Barrage || !FlecsSubsystem)
	{
		return Result;
	}

	Result.ElementKeys.Reserve(Count);
	Result.ConstraintKeys.Reserve(Count - 1);

	// Spawn elements
	for (int32 i = 0; i < Count; ++i)
	{
		FVector Location = StartLocation + SpacingVec * static_cast<float>(i);

		const uint32 Id = ++GGroupElementCounter;
		FSkeletonKey EntityKey = FSkeletonKey(FORGE_SKELETON_KEY(Id, SKELLY::SFIX_ITEM));

		FBarrageSpawnParams Params;
		Params.Mesh = Mesh;
		Params.WorldTransform = FTransform(Location);
		Params.EntityKey = EntityKey;
		Params.PhysicsLayer = EPhysicsLayer::MOVING;
		Params.bAutoCollider = true;
		Params.bIsMovable = true;
		Params.bDestructible = MaxHealth > 0.f;

		FBarrageSpawnResult SpawnResult = FBarrageSpawnUtils::SpawnEntity(World, Params);
		if (!SpawnResult.bSuccess)
		{
			Result.ElementKeys.Add(FSkeletonKey());
			continue;
		}

		if (Renderer)
		{
			Renderer->AddInstance(Mesh, nullptr, FTransform(Location), EntityKey);
		}

		Result.ElementKeys.Add(EntityKey);

		// Flecs entity
		FlecsSubsystem->EnqueueCommand([FlecsSubsystem, EntityKey, Mesh, MaxHealth]()
		{
			flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
			if (!FlecsWorld) return;

			flecs::entity Entity = FlecsWorld->entity()
				.set<FISMRender>({ Mesh, FVector::OneVector });

			if (MaxHealth > 0.f)
			{
				Entity.set<FHealthData>({ MaxHealth, MaxHealth, 0.f });
				Entity.add<FTagDestructible>();
			}

			FlecsSubsystem->BindEntityToBarrage(Entity, EntityKey);
		});
	}

	// Create constraints between adjacent elements
	for (int32 i = 0; i < Count - 1; ++i)
	{
		FSkeletonKey Key1 = Result.ElementKeys[i];
		FSkeletonKey Key2 = Result.ElementKeys[i + 1];

		if (!Key1.IsValid() || !Key2.IsValid())
		{
			Result.ConstraintKeys.Add(0);
			continue;
		}

		int64 ConstraintKey = CreateFixedConstraint(WorldContextObject, Key1, Key2, BreakForce, 0.f);
		Result.ConstraintKeys.Add(ConstraintKey);
	}

	Result.bSuccess = true;
	return Result;
}
