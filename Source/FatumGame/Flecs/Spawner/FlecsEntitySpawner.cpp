
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
#include "FlecsInteractionProfile.h"
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
	UFlecsInteractionProfile* EffectiveInteraction = Request.InteractionProfile;

	// Default tags from request
	bool bPickupable = Request.bPickupable;
	bool bDestructible = Request.bDestructible;
	bool bHasLoot = Request.bHasLoot;
	bool bIsCharacter = Request.bIsCharacter;
	bool bInteractable = Request.bInteractable;
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
		if (!EffectiveInteraction) EffectiveInteraction = Def->InteractionProfile;

		// Apply tags from definition (OR with request tags)
		bPickupable = bPickupable || Def->bPickupable;
		bDestructible = bDestructible || Def->bDestructible;
		bHasLoot = bHasLoot || Def->bHasLoot;
		bIsCharacter = bIsCharacter || Def->bIsCharacter;

		// InteractionProfile implies interactable
		bInteractable = bInteractable || EffectiveInteraction != nullptr;

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
		const float LinearDamping = EffectivePhysics ? EffectivePhysics->LinearDamping : 0.0f;
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
					Friction,
					LinearDamping
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
				Params.LinearDamping = LinearDamping;
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
		bool bInteractable;

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
	Data.bInteractable = bInteractable || EffectiveInteraction != nullptr;
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
		if (Data.bInteractable)
		{
			Entity.add<FTagInteractable>();
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

