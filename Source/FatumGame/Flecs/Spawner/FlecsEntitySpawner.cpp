
#include "FlecsEntitySpawner.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsEntityDefinition.h"
#include "FlecsItemDefinition.h"
#include "FlecsPhysicsProfile.h"
#include "FlecsRenderProfile.h"
#include "FlecsHealthProfile.h"
#include "FlecsDamageProfile.h"
#include "FlecsProjectileProfile.h"
#include "FlecsContainerProfile.h"
#include "FlecsGameTags.h"
#include "FlecsStaticComponents.h"
#include "FlecsInstanceComponents.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FBShapeParams.h"
#include "BarrageSpawnUtils.h"
#include "FlecsRenderManager.h"
#include "Skeletonize.h"
#include "flecs.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlecsEntity, Log, All);

// ═══════════════════════════════════════════════════════════════
// HELPER FUNCTIONS
// ═══════════════════════════════════════════════════════════════

namespace
{
	UFlecsArtillerySubsystem* GetFlecsSubsystem(const UObject* WorldContextObject)
	{
		if (!WorldContextObject)
		{
			return nullptr;
		}

		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
		if (!World)
		{
			return nullptr;
		}

		return World->GetSubsystem<UFlecsArtillerySubsystem>();
	}

	UWorld* GetWorldFromContext(const UObject* WorldContextObject)
	{
		if (!WorldContextObject)
		{
			return nullptr;
		}
		return GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	}

	// Convert EFlecsPhysicsLayer to EPhysicsLayer (Barrage layer)
	EPhysicsLayer ToBarrageLayer(EFlecsPhysicsLayer Layer)
	{
		switch (Layer)
		{
		case EFlecsPhysicsLayer::Static:     return EPhysicsLayer::NON_MOVING;
		case EFlecsPhysicsLayer::Moving:     return EPhysicsLayer::MOVING;
		case EFlecsPhysicsLayer::Projectile: return EPhysicsLayer::PROJECTILE;
		case EFlecsPhysicsLayer::Character:  return EPhysicsLayer::MOVING;
		case EFlecsPhysicsLayer::Trigger:    return EPhysicsLayer::MOVING;
		default:                             return EPhysicsLayer::MOVING;
		}
	}
}

// ═══════════════════════════════════════════════════════════════
// FEntitySpawnRequest methods
// ═══════════════════════════════════════════════════════════════

bool FEntitySpawnRequest::IsWorldEntity() const
{
	if (PhysicsProfile != nullptr || RenderProfile != nullptr)
	{
		return true;
	}
	if (EntityDefinition != nullptr)
	{
		return EntityDefinition->IsWorldEntity();
	}
	return false;
}

FSkeletonKey FEntitySpawnRequest::Spawn(UObject* WorldContext) const
{
	return UFlecsEntityLibrary::SpawnEntity(WorldContext, *this);
}

// ═══════════════════════════════════════════════════════════════
// SPAWNING
// ═══════════════════════════════════════════════════════════════

FSkeletonKey UFlecsEntityLibrary::SpawnEntity(
	UObject* WorldContextObject,
	const FEntitySpawnRequest& Request)
{
	// Validate request
	if (!Request.HasAnyProfile())
	{
		UE_LOG(LogFlecsEntity, Warning, TEXT("SpawnEntity: Request has no profiles set"));
		return FSkeletonKey();
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!FlecsSubsystem)
	{
		UE_LOG(LogFlecsEntity, Warning, TEXT("SpawnEntity: No FlecsArtillerySubsystem"));
		return FSkeletonKey();
	}

	UWorld* World = GetWorldFromContext(WorldContextObject);
	if (!World)
	{
		UE_LOG(LogFlecsEntity, Warning, TEXT("SpawnEntity: No World"));
		return FSkeletonKey();
	}

	// ─────────────────────────────────────────────────────────────
	// RESOLVE PROFILES (individual overrides take priority over EntityDefinition)
	// ─────────────────────────────────────────────────────────────
	UFlecsItemDefinition* EffectiveItem = Request.ItemDefinition;
	UFlecsPhysicsProfile* EffectivePhysics = Request.PhysicsProfile;
	UFlecsRenderProfile* EffectiveRender = Request.RenderProfile;
	UFlecsHealthProfile* EffectiveHealth = Request.HealthProfile;
	UFlecsDamageProfile* EffectiveDamage = Request.DamageProfile;
	UFlecsProjectileProfile* EffectiveProjectile = Request.ProjectileProfile;
	UFlecsContainerProfile* EffectiveContainer = Request.ContainerProfile;

	// Default tags from request
	bool bPickupable = Request.bPickupable;
	bool bDestructible = Request.bDestructible;
	bool bHasLoot = Request.bHasLoot;
	bool bIsCharacter = Request.bIsCharacter;
	int32 ItemCount = Request.ItemCount;
	float DespawnTime = Request.DespawnTime;

	// Apply EntityDefinition if set (individual profiles override)
	if (Request.EntityDefinition)
	{
		const UFlecsEntityDefinition* Def = Request.EntityDefinition;

		if (!EffectiveItem) EffectiveItem = Def->ItemDefinition;
		if (!EffectivePhysics) EffectivePhysics = Def->PhysicsProfile;
		if (!EffectiveRender) EffectiveRender = Def->RenderProfile;
		if (!EffectiveHealth) EffectiveHealth = Def->HealthProfile;
		if (!EffectiveDamage) EffectiveDamage = Def->DamageProfile;
		if (!EffectiveProjectile) EffectiveProjectile = Def->ProjectileProfile;
		if (!EffectiveContainer) EffectiveContainer = Def->ContainerProfile;

		// Apply tags from definition (OR with request tags)
		bPickupable = bPickupable || Def->bPickupable;
		bDestructible = bDestructible || Def->bDestructible;
		bHasLoot = bHasLoot || Def->bHasLoot;
		bIsCharacter = bIsCharacter || Def->bIsCharacter;

		// Use definition defaults if not overridden
		if (ItemCount == 1 && Def->DefaultItemCount > 1)
		{
			ItemCount = Def->DefaultItemCount;
		}
		if (DespawnTime < 0.f && Def->DefaultDespawnTime >= 0.f)
		{
			DespawnTime = Def->DefaultDespawnTime;
		}
	}

	FSkeletonKey EntityKey;

	// ─────────────────────────────────────────────────────────────
	// PHASE 1: Create physics body if needed (must be on game thread)
	// ─────────────────────────────────────────────────────────────
	if (EffectivePhysics || EffectiveRender)
	{
		// Validate that we have a mesh (required for ISM rendering)
		if (!EffectiveRender || !EffectiveRender->Mesh)
		{
			UE_LOG(LogFlecsEntity, Warning, TEXT("SpawnEntity: World entity requires RenderProfile with Mesh!"));
			return FSkeletonKey();
		}

		UBarrageDispatch* Barrage = World->GetSubsystem<UBarrageDispatch>();
		UFlecsRenderManager* Renderer = UFlecsRenderManager::Get(World);
		if (!Barrage)
		{
			UE_LOG(LogFlecsEntity, Error, TEXT("SpawnEntity: No UBarrageDispatch!"));
			return FSkeletonKey();
		}

		// ─────────────────────────────────────────────────────────
		// Determine physics shape type and settings
		// ─────────────────────────────────────────────────────────
		const bool bIsProjectile = EffectiveProjectile != nullptr;
		const bool bIsBouncing = bIsProjectile && EffectiveProjectile->IsBouncing();
		const float GravityFactor = EffectivePhysics ? EffectivePhysics->GravityFactor : 0.f;
		const float Friction = EffectivePhysics ? EffectivePhysics->Friction : 0.2f;
		const float Restitution = EffectivePhysics ? EffectivePhysics->Restitution : 0.3f;
		const float CollisionRadius = EffectivePhysics ? EffectivePhysics->CollisionRadius : 30.f;

		// Generate unique entity key
		if (bIsProjectile)
		{
			EntityKey = FBarrageSpawnUtils::GenerateUniqueKey(SKELLY::SFIX_GUN_SHOT);
		}
		else
		{
			EntityKey = FBarrageSpawnUtils::GenerateUniqueKey();
		}

		FBLet Body;

		if (bIsProjectile)
		{
			// ═══════════════════════════════════════════════════════
			// PROJECTILE: Use sphere collision
			// Dynamic body if: bouncing OR has gravity
			// Sensor if: no bouncing AND no gravity (laser-like)
			// ═══════════════════════════════════════════════════════
			const bool bNeedsDynamicBody = bIsBouncing || GravityFactor > 0.f;

			FBSphereParams SphereParams = FBarrageBounder::GenerateSphereBounds(Request.Location, CollisionRadius);

			if (bNeedsDynamicBody)
			{
				Body = Barrage->CreateBouncingSphere(
					SphereParams,
					EntityKey,
					static_cast<uint16>(EPhysicsLayer::PROJECTILE),
					bIsBouncing ? Restitution : 0.f,  // No restitution = stops on contact
					Friction
				);
			}
			else
			{
				Body = Barrage->CreatePrimitive(
					SphereParams,
					EntityKey,
					static_cast<uint16>(EPhysicsLayer::PROJECTILE),
					true  // IsSensor - laser-like, no physics
				);
			}

			UE_LOG(LogFlecsEntity, Log, TEXT("SpawnEntity: Projectile Key=%llu Bouncing=%d Gravity=%.2f Dynamic=%d"),
				static_cast<uint64>(EntityKey), bIsBouncing, GravityFactor, bNeedsDynamicBody);
		}
		else
		{
			// ═══════════════════════════════════════════════════════
			// NON-PROJECTILE: Use box collision (auto from mesh bounds)
			// ═══════════════════════════════════════════════════════
			FBarrageSpawnParams Params;
			Params.EntityKey = EntityKey;
			Params.WorldTransform = FTransform(Request.Rotation, Request.Location);
			Params.InitialVelocity = Request.InitialVelocity;
			Params.bIsMovable = true;

			if (EffectivePhysics)
			{
				Params.PhysicsLayer = ToBarrageLayer(EffectivePhysics->Layer);
				Params.bIsSensor = EffectivePhysics->bIsSensor;
				Params.GravityFactor = GravityFactor;
				Params.Friction = Friction;
				Params.Restitution = Restitution;
			}
			else
			{
				Params.PhysicsLayer = EPhysicsLayer::MOVING;
				Params.bIsSensor = true;
			}

			Params.Mesh = EffectiveRender->Mesh;
			Params.Material = EffectiveRender->MaterialOverride;
			Params.MeshScale = EffectiveRender->Scale;

			Params.bAutoCollider = true;

			FBarrageSpawnResult Result = FBarrageSpawnUtils::SpawnEntity(World, Params);
			if (!Result.bSuccess)
			{
				UE_LOG(LogFlecsEntity, Warning, TEXT("SpawnEntity: Failed to spawn box entity (Key=%llu)"),
					static_cast<uint64>(EntityKey));
				return FSkeletonKey();
			}

			// FBarrageSpawnUtils already added ISM, so skip manual ISM add below
			Body = Barrage->GetShapeRef(EntityKey);
		}

		// For projectiles, we need to manually set velocity/gravity and add ISM
		if (bIsProjectile)
		{
			if (!FBarragePrimitive::IsNotNull(Body))
			{
				UE_LOG(LogFlecsEntity, Error, TEXT("SpawnEntity: Failed to create projectile physics body!"));
				return FSkeletonKey();
			}

			// Set velocity and gravity
			if (!Request.InitialVelocity.IsNearlyZero())
			{
				FBarragePrimitive::SetVelocity(Request.InitialVelocity, Body);
			}
			FBarragePrimitive::SetGravityFactor(GravityFactor, Body);

			// Add ISM render instance
			if (Renderer)
			{
				FTransform RenderTransform;
				RenderTransform.SetLocation(Request.Location);
				RenderTransform.SetRotation(Request.Rotation.Quaternion() * EffectiveRender->RotationOffset.Quaternion());
				RenderTransform.SetScale3D(EffectiveRender->Scale);
				Renderer->AddInstance(EffectiveRender->Mesh, EffectiveRender->MaterialOverride, RenderTransform, EntityKey);
			}
		}

		UE_LOG(LogFlecsEntity, Log, TEXT("SpawnEntity: Spawned world entity Key=%llu at %s (Projectile=%d)"),
			static_cast<uint64>(EntityKey), *Request.Location.ToString(), bIsProjectile);
	}
	else
	{
		// Non-world entity (e.g., container without physical presence)
		// Generate a unique key
		static std::atomic<uint32> NonWorldEntityCounter{0};
		uint32 Counter = NonWorldEntityCounter.fetch_add(1);
		// Use ITEM nibble for non-world entities
		EntityKey = FSkeletonKey(0xC000000000000000ULL | Counter);
	}

	// ─────────────────────────────────────────────────────────────
	// PHASE 2: Create Flecs entity with components (on simulation thread)
	// Uses NEW prefab architecture: Static components on prefab, Instance on entity.
	// ─────────────────────────────────────────────────────────────

	// Capture data for lambda
	struct FSpawnData
	{
		FSkeletonKey Key;
		FVector Scale;
		bool bHasPhysics;
		bool bHasRender;
		UStaticMesh* Mesh;

		// EntityDefinition for prefab creation (safe - DataAssets persist)
		UFlecsEntityDefinition* EntityDefinition;

		// Instance-specific data (not in prefab)
		int32 ItemCount;
		float DespawnTime;
		float StartingHealth;  // Calculated from profile
		float Lifetime;        // Calculated from profile
		int32 GraceFrames;     // Calculated from profile

		// Container instance data
		bool bHasContainer;
		EContainerType ContainerType;
		int32 GridWidth;
		int32 GridHeight;
		int32 MaxListItems;

		// Tags
		bool bPickupable;
		bool bDestructible;
		bool bHasLoot;
		bool bIsCharacter;

		// Owner
		FSkeletonKey OwnerKey;
		int64 OwnerEntityId;  // For projectiles (friendly fire check)

		// Profile flags (for entities without EntityDefinition)
		bool bHasHealth;
		bool bHasDamage;
		bool bHasProjectile;
		bool bHasItem;
	};

	FSpawnData Data;
	Data.Key = EntityKey;
	Data.EntityDefinition = Request.EntityDefinition;

	// Physics binding needed if we created a physics body
	const bool bIsWorldEntity = (EffectivePhysics != nullptr) || (EffectiveRender != nullptr);
	Data.bHasPhysics = bIsWorldEntity && EntityKey.IsValid();

	// Render
	Data.bHasRender = EffectiveRender != nullptr && EffectiveRender->Mesh != nullptr;
	Data.Mesh = Data.bHasRender ? EffectiveRender->Mesh : nullptr;
	Data.Scale = Data.bHasRender ? EffectiveRender->Scale : FVector::OneVector;

	// Profile flags
	Data.bHasItem = EffectiveItem != nullptr;
	Data.bHasHealth = EffectiveHealth != nullptr;
	Data.bHasDamage = EffectiveDamage != nullptr;
	Data.bHasProjectile = EffectiveProjectile != nullptr;

	// Instance data
	Data.ItemCount = ItemCount;
	Data.DespawnTime = DespawnTime;
	Data.StartingHealth = Data.bHasHealth ? EffectiveHealth->GetStartingHealth() : 100.f;
	Data.Lifetime = Data.bHasProjectile ? EffectiveProjectile->Lifetime : 10.f;
	Data.GraceFrames = Data.bHasProjectile ? EffectiveProjectile->GetGraceFrames() : 30;

	// Container
	Data.bHasContainer = EffectiveContainer != nullptr;
	if (Data.bHasContainer)
	{
		Data.ContainerType = EffectiveContainer->ContainerType;
		Data.GridWidth = EffectiveContainer->GridWidth;
		Data.GridHeight = EffectiveContainer->GridHeight;
		Data.MaxListItems = EffectiveContainer->MaxListItems;
	}

	// Tags
	Data.bPickupable = bPickupable;
	Data.bDestructible = bDestructible;
	Data.bHasLoot = bHasLoot;
	Data.bIsCharacter = bIsCharacter;
	Data.OwnerKey = Request.OwnerKey;
	Data.OwnerEntityId = Request.OwnerEntityId;

	// Enqueue Flecs entity creation
	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Data]()
	{
		flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
		if (!FlecsWorld)
		{
			return;
		}

		flecs::entity Entity;

		// ─────────────────────────────────────────────────────────
		// CREATE ENTITY WITH PREFAB (if EntityDefinition available)
		// Static components are inherited from prefab via is_a()
		// ─────────────────────────────────────────────────────────
		if (Data.EntityDefinition)
		{
			flecs::entity Prefab = FlecsSubsystem->GetOrCreateEntityPrefab(Data.EntityDefinition);
			if (Prefab.is_valid())
			{
				Entity = FlecsWorld->entity().is_a(Prefab);
				UE_LOG(LogFlecsEntity, Log, TEXT("SpawnEntity PHASE2: Using prefab %llu for entity"),
					Prefab.id());
			}
			else
			{
				Entity = FlecsWorld->entity();
				UE_LOG(LogFlecsEntity, Warning, TEXT("SpawnEntity PHASE2: Failed to create prefab, using standalone entity"));
			}
		}
		else
		{
			Entity = FlecsWorld->entity();
		}

		// ─────────────────────────────────────────────────────────
		// PHYSICS BINDING (always on entity, not prefab)
		// ─────────────────────────────────────────────────────────
		if (Data.bHasPhysics)
		{
			FBarrageBody Body;
			Body.BarrageKey = Data.Key;
			Entity.set<FBarrageBody>(Body);
			FlecsSubsystem->BindEntityToBarrage(Entity, Data.Key);
			UE_LOG(LogFlecsEntity, Log, TEXT("SpawnEntity PHASE2: Created Flecs entity %llu, bound to Key=%llu"),
				Entity.id(), static_cast<uint64>(Data.Key));
		}
		else
		{
			UE_LOG(LogFlecsEntity, Warning, TEXT("SpawnEntity PHASE2: Created Flecs entity %llu WITHOUT physics binding!"),
				Entity.id());
		}

		// ─────────────────────────────────────────────────────────
		// RENDER (always on entity, not prefab)
		// ─────────────────────────────────────────────────────────
		if (Data.bHasRender)
		{
			FISMRender Render;
			Render.Mesh = Data.Mesh;
			Render.Scale = Data.Scale;
			Entity.set<FISMRender>(Render);
		}

		// ─────────────────────────────────────────────────────────
		// INSTANCE COMPONENTS (mutable per-entity data)
		// Static data comes from prefab via is_a() inheritance
		// ─────────────────────────────────────────────────────────

		// Health instance
		if (Data.bHasHealth)
		{
			FHealthInstance HealthInst;
			HealthInst.CurrentHP = Data.StartingHealth;
			HealthInst.RegenAccumulator = 0.f;
			Entity.set<FHealthInstance>(HealthInst);
		}

		// Projectile instance
		if (Data.bHasProjectile)
		{
			FProjectileInstance ProjInst;
			ProjInst.LifetimeRemaining = Data.Lifetime;
			ProjInst.BounceCount = 0;
			ProjInst.GraceFramesRemaining = Data.GraceFrames;
			ProjInst.OwnerEntityId = Data.OwnerEntityId;
			Entity.set<FProjectileInstance>(ProjInst);
			Entity.add<FTagProjectile>();
		}

		// Item instance
		if (Data.bHasItem)
		{
			FItemInstance ItemInst;
			ItemInst.Count = Data.ItemCount;
			Entity.set<FItemInstance>(ItemInst);
			Entity.add<FTagItem>();

			// World item data for despawn/pickup
			if (Data.DespawnTime > 0.f || Data.bPickupable)
			{
				FWorldItemInstance WorldInst;
				WorldInst.DespawnTimer = Data.DespawnTime;
				WorldInst.PickupGraceTimer = 0.f;
				WorldInst.DroppedByEntityId = 0;
				Entity.set<FWorldItemInstance>(WorldInst);
			}
		}

		// Container instance
		if (Data.bHasContainer)
		{
			int64 OwnerEntityId = 0;
			if (Data.OwnerKey.IsValid())
			{
				flecs::entity OwnerEntity = FlecsSubsystem->GetEntityForBarrageKey(Data.OwnerKey);
				if (OwnerEntity.is_valid())
				{
					OwnerEntityId = static_cast<int64>(OwnerEntity.id());
				}
			}

			FContainerInstance ContainerInst;
			ContainerInst.CurrentWeight = 0.f;
			ContainerInst.CurrentCount = 0;
			ContainerInst.OwnerEntityId = OwnerEntityId;
			Entity.set<FContainerInstance>(ContainerInst);
			Entity.add<FTagContainer>();

			// Type-specific instance components
			switch (Data.ContainerType)
			{
			case EContainerType::Grid:
				{
					FContainerGridInstance GridInst;
					GridInst.Initialize(Data.GridWidth, Data.GridHeight);
					Entity.set<FContainerGridInstance>(GridInst);
				}
				break;
			case EContainerType::Slot:
				{
					FContainerSlotsInstance SlotsInst;
					Entity.set<FContainerSlotsInstance>(SlotsInst);
				}
				break;
			case EContainerType::List:
				// List uses ContainerInstance.CurrentCount, no extra component needed
				break;
			}
		}

		// ─────────────────────────────────────────────────────────
		// TAGS (zero-size markers)
		// ─────────────────────────────────────────────────────────
		if (Data.bPickupable)
		{
			Entity.add<FTagPickupable>();
		}
		if (Data.bDestructible)
		{
			Entity.add<FTagDestructible>();
		}
		if (Data.bHasLoot)
		{
			Entity.add<FTagHasLoot>();
		}
		if (Data.bIsCharacter)
		{
			Entity.add<FTagCharacter>();
		}

		UE_LOG(LogFlecsEntity, Verbose, TEXT("Spawned entity: Key=%llu FlecsId=%llu Item=%d Health=%d Projectile=%d Container=%d Prefab=%d"),
			static_cast<uint64>(Data.Key), Entity.id(),
			Data.bHasItem, Data.bHasHealth, Data.bHasProjectile, Data.bHasContainer,
			Data.EntityDefinition != nullptr);
	});

	return EntityKey;
}

TArray<FSkeletonKey> UFlecsEntityLibrary::SpawnEntities(
	UObject* WorldContextObject,
	const TArray<FEntitySpawnRequest>& Requests)
{
	TArray<FSkeletonKey> Results;
	Results.Reserve(Requests.Num());

	for (const FEntitySpawnRequest& Request : Requests)
	{
		Results.Add(SpawnEntity(WorldContextObject, Request));
	}

	return Results;
}

FSkeletonKey UFlecsEntityLibrary::SpawnEntityFromDefinition(
	UObject* WorldContextObject,
	UFlecsEntityDefinition* Definition,
	FVector Location,
	FRotator Rotation)
{
	if (!Definition)
	{
		UE_LOG(LogFlecsEntity, Warning, TEXT("SpawnEntityFromDefinition: Definition is null"));
		return FSkeletonKey();
	}

	return SpawnEntity(WorldContextObject, FEntitySpawnRequest::FromDefinition(Definition, Location, Rotation));
}

// ═══════════════════════════════════════════════════════════════
// DESTRUCTION
// ═══════════════════════════════════════════════════════════════

void UFlecsEntityLibrary::DestroyEntity(
	UObject* WorldContextObject,
	FSkeletonKey EntityKey)
{
	if (!EntityKey.IsValid())
	{
		UE_LOG(LogFlecsEntity, Warning, TEXT("DestroyEntity: Invalid EntityKey!"));
		return;
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!FlecsSubsystem)
	{
		UE_LOG(LogFlecsEntity, Warning, TEXT("DestroyEntity: No FlecsSubsystem!"));
		return;
	}

	UE_LOG(LogFlecsEntity, Log, TEXT("DestroyEntity: Enqueueing destruction for Key=%llu"), static_cast<uint64>(EntityKey));

	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, EntityKey]()
	{
		flecs::entity Entity = FlecsSubsystem->GetEntityForBarrageKey(EntityKey);
		if (Entity.is_valid())
		{
			UE_LOG(LogFlecsEntity, Log, TEXT("DestroyEntity: Found Flecs entity %llu for Key=%llu, adding FTagDead"),
				Entity.id(), static_cast<uint64>(EntityKey));
			Entity.add<FTagDead>();
		}
		else
		{
			// CRITICAL: Entity not found! This means binding was not done or key mismatch.
			UE_LOG(LogFlecsEntity, Error, TEXT("DestroyEntity: FAILED to find Flecs entity for Key=%llu! "
				"Binding not done or key mismatch. Physics body will NOT be destroyed!"),
				static_cast<uint64>(EntityKey));

			// FALLBACK: Try to destroy physics directly via Barrage
			// This is a safety net, not a fix for the root cause
			if (UBarrageDispatch* Barrage = UBarrageDispatch::SelfPtr)
			{
				FBLet Prim = Barrage->GetShapeRef(EntityKey);
				if (FBarragePrimitive::IsNotNull(Prim))
				{
					UE_LOG(LogFlecsEntity, Warning, TEXT("DestroyEntity: Fallback - destroying physics directly for Key=%llu"),
						static_cast<uint64>(EntityKey));

					// Remove ISM
					if (UFlecsRenderManager* Renderer = UFlecsRenderManager::Get(Barrage->GetWorld()))
					{
						Renderer->RemoveInstance(EntityKey);
					}
					// Destroy physics
					Barrage->SuggestTombstone(Prim);
				}
				else
				{
					UE_LOG(LogFlecsEntity, Error, TEXT("DestroyEntity: Fallback FAILED - no physics body for Key=%llu"),
						static_cast<uint64>(EntityKey));
				}
			}
		}
	});
}

void UFlecsEntityLibrary::DestroyEntities(
	UObject* WorldContextObject,
	const TArray<FSkeletonKey>& EntityKeys)
{
	for (const FSkeletonKey& Key : EntityKeys)
	{
		DestroyEntity(WorldContextObject, Key);
	}
}

// ═══════════════════════════════════════════════════════════════
// QUERIES
// ═══════════════════════════════════════════════════════════════

bool UFlecsEntityLibrary::IsEntityAlive(
	UObject* WorldContextObject,
	FSkeletonKey EntityKey)
{
	if (!EntityKey.IsValid())
	{
		return false;
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!FlecsSubsystem)
	{
		return false;
	}

	flecs::entity Entity = FlecsSubsystem->GetEntityForBarrageKey(EntityKey);
	if (!Entity.is_valid())
	{
		return false;
	}

	return !Entity.has<FTagDead>();
}

int64 UFlecsEntityLibrary::GetEntityId(
	UObject* WorldContextObject,
	FSkeletonKey EntityKey)
{
	if (!EntityKey.IsValid())
	{
		return 0;
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!FlecsSubsystem)
	{
		return 0;
	}

	flecs::entity Entity = FlecsSubsystem->GetEntityForBarrageKey(EntityKey);
	return Entity.is_valid() ? static_cast<int64>(Entity.id()) : 0;
}

// ═══════════════════════════════════════════════════════════════
// ITEM OPERATIONS
// ═══════════════════════════════════════════════════════════════

bool UFlecsEntityLibrary::AddItemToContainerFromDefinition(
	UObject* WorldContextObject,
	FSkeletonKey ContainerKey,
	UFlecsEntityDefinition* EntityDefinition,
	int32 Count,
	int32& OutActuallyAdded)
{
	OutActuallyAdded = 0;

	if (!ContainerKey.IsValid() || !EntityDefinition || Count <= 0)
	{
		UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainerFromDefinition: Invalid parameters"));
		return false;
	}

	UFlecsItemDefinition* ItemDef = EntityDefinition->ItemDefinition;
	if (!ItemDef)
	{
		UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainerFromDefinition: EntityDefinition '%s' has no ItemDefinition"),
			*EntityDefinition->GetName());
		return false;
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!FlecsSubsystem)
	{
		UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainerFromDefinition: No FlecsSubsystem"));
		return false;
	}

	// Capture EntityDefinition pointer for lambda (safe - DataAssets persist for game lifetime)
	// We use raw pointer because DataAssets are loaded and kept in memory
	UFlecsEntityDefinition* CapturedEntityDef = EntityDefinition;
	const FName ItemName = ItemDef->ItemName;

	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, ContainerKey, CapturedEntityDef, ItemName, Count]()
	{
		flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
		if (!FlecsWorld)
		{
			return;
		}

		// Get or create prefab for this item type
		flecs::entity ItemPrefab = FlecsSubsystem->GetOrCreateItemPrefab(CapturedEntityDef);
		if (!ItemPrefab.is_valid())
		{
			UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainerFromDefinition: Failed to create prefab for '%s'"),
				*ItemName.ToString());
			return;
		}

		// Find container entity
		flecs::entity ContainerEntity = FlecsSubsystem->GetEntityForBarrageKey(ContainerKey);
		if (!ContainerEntity.is_valid())
		{
			UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainerFromDefinition: Container entity not found for Key=%llu"),
				static_cast<uint64>(ContainerKey));
			return;
		}

		// Verify it's a container
		if (!ContainerEntity.has<FTagContainer>())
		{
			UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainerFromDefinition: Entity %llu is not a container"),
				ContainerEntity.id());
			return;
		}

		// Get container static data (from prefab)
		const FContainerStatic* ContainerStatic = ContainerEntity.try_get<FContainerStatic>();
		if (!ContainerStatic)
		{
			UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainerFromDefinition: Container %llu has no FContainerStatic"),
				ContainerEntity.id());
			return;
		}

		// Get container instance data
		FContainerInstance* ContainerInstance = ContainerEntity.try_get_mut<FContainerInstance>();
		if (!ContainerInstance)
		{
			UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainerFromDefinition: Container %llu has no FContainerInstance"),
				ContainerEntity.id());
			return;
		}

		// Helper: Create item entity with prefab
		auto CreateItemEntity = [&](FIntPoint GridPos, int32 SlotIdx) -> flecs::entity
		{
			FItemInstance Instance;
			Instance.Count = Count;

			FContainedIn Contained;
			Contained.ContainerEntityId = static_cast<int64>(ContainerEntity.id());
			Contained.GridPosition = GridPos;
			Contained.SlotIndex = SlotIdx;

			// Create entity with is_a(prefab) - inherits FItemStaticData
			flecs::entity ItemEntity = FlecsWorld->entity()
				.is_a(ItemPrefab)  // Inherit static data from prefab
				.set<FItemInstance>(Instance)
				.set<FContainedIn>(Contained)
				.add<FTagItem>();

			return ItemEntity;
		};

		// For List containers, check capacity
		if (ContainerStatic->Type == EContainerType::List)
		{
			// Check if full
			if (ContainerStatic->MaxItems > 0 && ContainerInstance->CurrentCount >= ContainerStatic->MaxItems)
			{
				UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainerFromDefinition: List container %llu is full"),
					ContainerEntity.id());
				return;
			}

			// Create item entity with prefab
			flecs::entity ItemEntity = CreateItemEntity(
				FIntPoint(-1, -1),  // Not in grid
				ContainerInstance->CurrentCount  // Slot index
			);

			// Update list count
			ContainerInstance->CurrentCount++;

			UE_LOG(LogFlecsEntity, Log, TEXT("AddItemToContainerFromDefinition: Added item '%s' (Count=%d) to container %llu. ItemEntity=%llu (prefab=%llu)"),
				*ItemName.ToString(), Count, ContainerEntity.id(), ItemEntity.id(), ItemPrefab.id());
		}
		else if (ContainerStatic->Type == EContainerType::Grid)
		{
			// For Grid containers, find free space
			FContainerGridInstance* GridInstance = ContainerEntity.try_get_mut<FContainerGridInstance>();
			if (!GridInstance)
			{
				UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainerFromDefinition: Grid container %llu has no FContainerGridInstance"),
					ContainerEntity.id());
				return;
			}

			// Get item grid size from prefab's static data
			const FItemStaticData* StaticData = ItemPrefab.try_get<FItemStaticData>();
			FIntPoint ItemSize = StaticData ? StaticData->GridSize : FIntPoint(1, 1);

			FIntPoint FreePos = GridInstance->FindFreeSpace(ItemSize, ContainerStatic->GridWidth, ContainerStatic->GridHeight);
			if (FreePos.X < 0)
			{
				UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainerFromDefinition: Grid container %llu is full"),
					ContainerEntity.id());
				return;
			}

			// Occupy the cells
			GridInstance->Occupy(FreePos, ItemSize, ContainerStatic->GridWidth);

			// Create item entity with prefab
			flecs::entity ItemEntity = CreateItemEntity(FreePos, -1);

			// Update container count
			ContainerInstance->CurrentCount++;

			UE_LOG(LogFlecsEntity, Log, TEXT("AddItemToContainerFromDefinition: Added item '%s' to grid container %llu at (%d,%d). ItemEntity=%llu (prefab=%llu)"),
				*ItemName.ToString(), ContainerEntity.id(), FreePos.X, FreePos.Y, ItemEntity.id(), ItemPrefab.id());
		}
		else
		{
			UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainerFromDefinition: Unsupported container type %d"),
				static_cast<int32>(ContainerStatic->Type));
		}
	});

	// Note: OutActuallyAdded is set synchronously but actual add is async
	OutActuallyAdded = Count;
	return true;
}

bool UFlecsEntityLibrary::AddItemToContainer(
	UObject* WorldContextObject,
	FSkeletonKey ContainerKey,
	UFlecsItemDefinition* ItemDefinition,
	int32 Count,
	int32& OutActuallyAdded)
{
	OutActuallyAdded = 0;

	if (!ContainerKey.IsValid() || !ItemDefinition || Count <= 0)
	{
		UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainer: Invalid parameters"));
		return false;
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!FlecsSubsystem)
	{
		UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainer: No FlecsSubsystem"));
		return false;
	}

	// Capture data for lambda (legacy - no prefab, direct TypeId)
	const int32 ItemTypeId = ItemDefinition->ItemTypeId != 0 ? ItemDefinition->ItemTypeId : GetTypeHash(ItemDefinition->ItemName);
	const FName ItemName = ItemDefinition->ItemName;

	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, ContainerKey, ItemTypeId, ItemName, Count]()
	{
		flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
		if (!FlecsWorld)
		{
			return;
		}

		// Find container entity
		flecs::entity ContainerEntity = FlecsSubsystem->GetEntityForBarrageKey(ContainerKey);
		if (!ContainerEntity.is_valid())
		{
			UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainer: Container entity not found for Key=%llu"),
				static_cast<uint64>(ContainerKey));
			return;
		}

		// Verify it's a container
		if (!ContainerEntity.has<FTagContainer>())
		{
			UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainer: Entity %llu is not a container"),
				ContainerEntity.id());
			return;
		}

		// Get container static data (from prefab)
		const FContainerStatic* ContainerStatic = ContainerEntity.try_get<FContainerStatic>();
		if (!ContainerStatic)
		{
			UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainer: Container %llu has no FContainerStatic"),
				ContainerEntity.id());
			return;
		}

		// Get container instance data
		FContainerInstance* ContainerInstance = ContainerEntity.try_get_mut<FContainerInstance>();
		if (!ContainerInstance)
		{
			UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainer: Container %llu has no FContainerInstance"),
				ContainerEntity.id());
			return;
		}

		// For List containers, check capacity
		if (ContainerStatic->Type == EContainerType::List)
		{
			// Check if full
			if (ContainerStatic->MaxItems > 0 && ContainerInstance->CurrentCount >= ContainerStatic->MaxItems)
			{
				UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainer: List container %llu is full"),
					ContainerEntity.id());
				return;
			}

			// Create item entity (legacy - no prefab, stores TypeId directly)
			// NOTE: This approach loses EntityDefinition reference!
			FItemStaticData StaticData;
			StaticData.TypeId = ItemTypeId;
			StaticData.MaxStack = 99;
			StaticData.Weight = 0.1f;
			StaticData.GridSize = FIntPoint(1, 1);
			StaticData.ItemName = ItemName;
			StaticData.EntityDefinition = nullptr;
			StaticData.ItemDefinition = nullptr;

			FItemInstance Instance;
			Instance.Count = Count;

			FContainedIn Contained;
			Contained.ContainerEntityId = static_cast<int64>(ContainerEntity.id());
			Contained.GridPosition = FIntPoint(-1, -1);
			Contained.SlotIndex = ContainerInstance->CurrentCount;

			flecs::entity ItemEntity = FlecsWorld->entity()
				.set<FItemStaticData>(StaticData)
				.set<FItemInstance>(Instance)
				.set<FContainedIn>(Contained)
				.add<FTagItem>();

			ContainerInstance->CurrentCount++;

			UE_LOG(LogFlecsEntity, Log, TEXT("AddItemToContainer (legacy): Added item '%s' (TypeId=%d, Count=%d) to container %llu. ItemEntity=%llu"),
				*ItemName.ToString(), ItemTypeId, Count, ContainerEntity.id(), ItemEntity.id());
		}
		else if (ContainerStatic->Type == EContainerType::Grid)
		{
			FContainerGridInstance* GridInstance = ContainerEntity.try_get_mut<FContainerGridInstance>();
			if (!GridInstance)
			{
				UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainer: Grid container %llu has no FContainerGridInstance"),
					ContainerEntity.id());
				return;
			}

			FIntPoint FreePos = GridInstance->FindFreeSpace(FIntPoint(1, 1), ContainerStatic->GridWidth, ContainerStatic->GridHeight);
			if (FreePos.X < 0)
			{
				UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainer: Grid container %llu is full"),
					ContainerEntity.id());
				return;
			}

			GridInstance->Occupy(FreePos, FIntPoint(1, 1), ContainerStatic->GridWidth);

			FItemStaticData StaticData;
			StaticData.TypeId = ItemTypeId;
			StaticData.MaxStack = 99;
			StaticData.Weight = 0.1f;
			StaticData.GridSize = FIntPoint(1, 1);
			StaticData.ItemName = ItemName;
			StaticData.EntityDefinition = nullptr;
			StaticData.ItemDefinition = nullptr;

			FItemInstance Instance;
			Instance.Count = Count;

			FContainedIn Contained;
			Contained.ContainerEntityId = static_cast<int64>(ContainerEntity.id());
			Contained.GridPosition = FreePos;
			Contained.SlotIndex = -1;

			flecs::entity ItemEntity = FlecsWorld->entity()
				.set<FItemStaticData>(StaticData)
				.set<FItemInstance>(Instance)
				.set<FContainedIn>(Contained)
				.add<FTagItem>();

			ContainerInstance->CurrentCount++;

			UE_LOG(LogFlecsEntity, Log, TEXT("AddItemToContainer (legacy): Added item '%s' to grid container %llu at (%d,%d). ItemEntity=%llu"),
				*ItemName.ToString(), ContainerEntity.id(), FreePos.X, FreePos.Y, ItemEntity.id());
		}
		else
		{
			UE_LOG(LogFlecsEntity, Warning, TEXT("AddItemToContainer: Unsupported container type %d"),
				static_cast<int32>(ContainerStatic->Type));
		}
	});

	OutActuallyAdded = Count;
	return true;
}

bool UFlecsEntityLibrary::RemoveItemFromContainer(
	UObject* WorldContextObject,
	FSkeletonKey ContainerKey,
	int64 ItemEntityId,
	int32 Count)
{
	if (!ContainerKey.IsValid() || ItemEntityId == 0)
	{
		UE_LOG(LogFlecsEntity, Warning, TEXT("RemoveItemFromContainer: Invalid parameters"));
		return false;
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!FlecsSubsystem)
	{
		return false;
	}

	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, ContainerKey, ItemEntityId, Count]()
	{
		flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
		if (!FlecsWorld)
		{
			return;
		}

		// Find container
		flecs::entity ContainerEntity = FlecsSubsystem->GetEntityForBarrageKey(ContainerKey);
		if (!ContainerEntity.is_valid())
		{
			return;
		}

		// Find item entity
		flecs::entity ItemEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(ItemEntityId));
		if (!ItemEntity.is_valid())
		{
			UE_LOG(LogFlecsEntity, Warning, TEXT("RemoveItemFromContainer: Item entity %lld not found"), ItemEntityId);
			return;
		}

		// Verify item is in this container
		const FContainedIn* ContainedIn = ItemEntity.try_get<FContainedIn>();
		if (!ContainedIn || ContainedIn->ContainerEntityId != static_cast<int64>(ContainerEntity.id()))
		{
			UE_LOG(LogFlecsEntity, Warning, TEXT("RemoveItemFromContainer: Item %lld not in container %llu"),
				ItemEntityId, ContainerEntity.id());
			return;
		}

		// Free grid space if needed and update counter
		const FContainerStatic* ContainerStatic = ContainerEntity.try_get<FContainerStatic>();
		FContainerInstance* ContainerInstance = ContainerEntity.try_get_mut<FContainerInstance>();

		if (ContainerStatic && ContainerStatic->Type == EContainerType::Grid && ContainedIn->IsInGrid())
		{
			if (FContainerGridInstance* GridInstance = ContainerEntity.try_get_mut<FContainerGridInstance>())
			{
				GridInstance->Free(ContainedIn->GridPosition, FIntPoint(1, 1), ContainerStatic->GridWidth);
			}
		}

		// Decrement counter
		if (ContainerInstance)
		{
			ContainerInstance->CurrentCount = FMath::Max(0, ContainerInstance->CurrentCount - 1);
		}

		// Destroy item entity
		UE_LOG(LogFlecsEntity, Log, TEXT("RemoveItemFromContainer: Removed item %lld from container %llu"),
			ItemEntityId, ContainerEntity.id());
		ItemEntity.destruct();
	});

	return true;
}

int32 UFlecsEntityLibrary::RemoveAllItemsFromContainer(
	UObject* WorldContextObject,
	FSkeletonKey ContainerKey)
{
	if (!ContainerKey.IsValid())
	{
		UE_LOG(LogFlecsEntity, Warning, TEXT("RemoveAllItemsFromContainer: Invalid ContainerKey"));
		return 0;
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!FlecsSubsystem)
	{
		return 0;
	}

	// Note: We return count asynchronously via log, actual removal is async
	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, ContainerKey]()
	{
		flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
		if (!FlecsWorld)
		{
			return;
		}

		// Find container entity
		flecs::entity ContainerEntity = FlecsSubsystem->GetEntityForBarrageKey(ContainerKey);
		if (!ContainerEntity.is_valid())
		{
			UE_LOG(LogFlecsEntity, Warning, TEXT("RemoveAllItemsFromContainer: Container not found for Key=%llu"),
				static_cast<uint64>(ContainerKey));
			return;
		}

		const int64 ContainerId = static_cast<int64>(ContainerEntity.id());

		// Collect items to remove (can't modify while iterating)
		TArray<flecs::entity> ItemsToRemove;

		FlecsWorld->each([ContainerId, &ItemsToRemove](flecs::entity E, const FContainedIn& ContainedIn)
		{
			if (ContainedIn.ContainerEntityId == ContainerId)
			{
				ItemsToRemove.Add(E);
			}
		});

		// Remove all items
		for (flecs::entity ItemEntity : ItemsToRemove)
		{
			ItemEntity.destruct();
		}

		// Reset container counters
		const FContainerStatic* ContainerStatic = ContainerEntity.try_get<FContainerStatic>();
		FContainerInstance* ContainerInstance = ContainerEntity.try_get_mut<FContainerInstance>();

		if (ContainerInstance)
		{
			ContainerInstance->CurrentCount = 0;
		}

		// Reset grid if applicable
		if (ContainerStatic && ContainerStatic->Type == EContainerType::Grid)
		{
			if (FContainerGridInstance* GridInstance = ContainerEntity.try_get_mut<FContainerGridInstance>())
			{
				GridInstance->Initialize(ContainerStatic->GridWidth, ContainerStatic->GridHeight);
			}
		}

		UE_LOG(LogFlecsEntity, Log, TEXT("RemoveAllItemsFromContainer: Removed %d items from container %llu"),
			ItemsToRemove.Num(), ContainerEntity.id());
	});

	return 0;  // Async - actual count logged
}

int32 UFlecsEntityLibrary::GetContainerItemCount(
	UObject* WorldContextObject,
	FSkeletonKey ContainerKey)
{
	if (!ContainerKey.IsValid())
	{
		return -1;
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!FlecsSubsystem || !FlecsSubsystem->GetFlecsWorld())
	{
		return -1;
	}

	flecs::entity ContainerEntity = FlecsSubsystem->GetEntityForBarrageKey(ContainerKey);
	if (!ContainerEntity.is_valid() || !ContainerEntity.has<FTagContainer>())
	{
		return -1;
	}

	const FContainerInstance* ContainerInstance = ContainerEntity.try_get<FContainerInstance>();
	return ContainerInstance ? ContainerInstance->CurrentCount : 0;
}

bool UFlecsEntityLibrary::PickupItem(
	UObject* WorldContextObject,
	FSkeletonKey WorldItemKey,
	FSkeletonKey ContainerKey,
	int32& OutPickedUp)
{
	OutPickedUp = 0;

	if (!WorldItemKey.IsValid() || !ContainerKey.IsValid())
	{
		return false;
	}

	// TODO: Implement via simulation thread
	UE_LOG(LogFlecsEntity, Warning, TEXT("PickupItem: Not yet implemented"));
	return false;
}

FSkeletonKey UFlecsEntityLibrary::DropItem(
	UObject* WorldContextObject,
	FSkeletonKey ContainerKey,
	int64 ItemEntityId,
	FVector DropLocation,
	int32 Count)
{
	if (!ContainerKey.IsValid() || ItemEntityId == 0)
	{
		return FSkeletonKey();
	}

	// TODO: Implement via simulation thread
	UE_LOG(LogFlecsEntity, Warning, TEXT("DropItem: Not yet implemented"));
	return FSkeletonKey();
}

// ═══════════════════════════════════════════════════════════════
// HEALTH OPERATIONS
// Uses Static/Instance architecture.
// ═══════════════════════════════════════════════════════════════

bool UFlecsEntityLibrary::ApplyDamage(
	UObject* WorldContextObject,
	FSkeletonKey TargetKey,
	float Damage)
{
	if (!TargetKey.IsValid() || Damage <= 0.f)
	{
		return false;
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!FlecsSubsystem)
	{
		return false;
	}

	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, TargetKey, Damage]()
	{
		flecs::entity Entity = FlecsSubsystem->GetEntityForBarrageKey(TargetKey);
		if (!Entity.is_valid())
		{
			return;
		}

		FHealthInstance* HealthInst = Entity.try_get_mut<FHealthInstance>();
		if (HealthInst)
		{
			const FHealthStatic* HealthStatic = Entity.try_get<FHealthStatic>();
			float Armor = HealthStatic ? HealthStatic->Armor : 0.f;
			float EffectiveDamage = FMath::Max(0.f, Damage - Armor);
			HealthInst->CurrentHP -= EffectiveDamage;
		}
	});

	return true;
}

bool UFlecsEntityLibrary::Heal(
	UObject* WorldContextObject,
	FSkeletonKey TargetKey,
	float Amount)
{
	if (!TargetKey.IsValid() || Amount <= 0.f)
	{
		return false;
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!FlecsSubsystem)
	{
		return false;
	}

	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, TargetKey, Amount]()
	{
		flecs::entity Entity = FlecsSubsystem->GetEntityForBarrageKey(TargetKey);
		if (!Entity.is_valid())
		{
			return;
		}

		FHealthInstance* HealthInst = Entity.try_get_mut<FHealthInstance>();
		if (HealthInst)
		{
			const FHealthStatic* HealthStatic = Entity.try_get<FHealthStatic>();
			float MaxHP = HealthStatic ? HealthStatic->MaxHP : 100.f;
			HealthInst->CurrentHP = FMath::Min(HealthInst->CurrentHP + Amount, MaxHP);
		}
	});

	return true;
}

void UFlecsEntityLibrary::Kill(
	UObject* WorldContextObject,
	FSkeletonKey TargetKey)
{
	if (!TargetKey.IsValid())
	{
		return;
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!FlecsSubsystem)
	{
		return;
	}

	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, TargetKey]()
	{
		flecs::entity Entity = FlecsSubsystem->GetEntityForBarrageKey(TargetKey);
		if (Entity.is_valid())
		{
			Entity.add<FTagDead>();
		}
	});
}

float UFlecsEntityLibrary::GetHealth(
	UObject* WorldContextObject,
	FSkeletonKey EntityKey)
{
	if (!EntityKey.IsValid())
	{
		return 0.f;
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!FlecsSubsystem || !FlecsSubsystem->GetFlecsWorld())
	{
		return 0.f;
	}

	flecs::entity Entity = FlecsSubsystem->GetEntityForBarrageKey(EntityKey);
	if (!Entity.is_valid())
	{
		return 0.f;
	}

	const FHealthInstance* HealthInst = Entity.try_get<FHealthInstance>();
	return HealthInst ? HealthInst->CurrentHP : 0.f;
}

float UFlecsEntityLibrary::GetMaxHealth(
	UObject* WorldContextObject,
	FSkeletonKey EntityKey)
{
	if (!EntityKey.IsValid())
	{
		return 0.f;
	}

	UFlecsArtillerySubsystem* FlecsSubsystem = GetFlecsSubsystem(WorldContextObject);
	if (!FlecsSubsystem || !FlecsSubsystem->GetFlecsWorld())
	{
		return 0.f;
	}

	flecs::entity Entity = FlecsSubsystem->GetEntityForBarrageKey(EntityKey);
	if (!Entity.is_valid())
	{
		return 0.f;
	}

	const FHealthStatic* HealthStatic = Entity.try_get<FHealthStatic>();
	return HealthStatic ? HealthStatic->MaxHP : 0.f;
}
