
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
#include "FlecsDoorProfile.h"
#include "FlecsDoorComponents.h"
#include "FlecsGameTags.h"
#include "FlecsStaticComponents.h"
#include "FlecsInstanceComponents.h"
#include "BarrageDispatch.h"
#include "BarrageConstraintSystem.h"
#include "FBarragePrimitive.h"
#include "FBShapeParams.h"
#include "FBConstraintParams.h"
#include "BarrageSpawnUtils.h"
#include "FlecsRenderManager.h"
#include "FlecsNiagaraManager.h"
#include "FlecsNiagaraProfile.h"
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
	UFlecsNiagaraProfile* EffectiveNiagara = Request.NiagaraProfile;
	UFlecsDoorProfile* EffectiveDoor = nullptr;

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
		if (!EffectiveNiagara) EffectiveNiagara = Def->NiagaraProfile;
		if (!EffectiveDoor) EffectiveDoor = Def->DoorProfile;

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
			// ALL projectiles use dynamic body — sensors tunnel at high speed (no CCD).
			// Non-bouncing: restitution=0 (stops on contact, killed by OnBarrageContact).
			// ═══════════════════════════════════════════════════════
			FBSphereParams SphereParams = FBarrageBounder::GenerateSphereBounds(Request.Location, CollisionRadius);

			Body = Barrage->CreateBouncingSphere(
				SphereParams,
				EntityKey,
				static_cast<uint16>(EPhysicsLayer::PROJECTILE),
				bIsBouncing ? Restitution : 0.f,
				Friction,
				LinearDamping
			);

			UE_LOG(LogFlecsEntity, Log, TEXT("SpawnEntity: Projectile Key=%llu Bouncing=%d Gravity=%.2f"),
				static_cast<uint64>(EntityKey), bIsBouncing, GravityFactor);
		}
		else
		{
			// ═══════════════════════════════════════════════════════
			// NON-PROJECTILE: Use box collision (auto from mesh bounds)
			// ═══════════════════════════════════════════════════════
			const bool bIsDoor = EffectiveDoor != nullptr;

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

			// Doors must be Dynamic from creation (Jolt Static bodies lack MotionProperties,
			// so SetBodyMass/SetBodyAngularDamping would crash on null).
			// AllowedDOFs = 0x3F (All) bypasses MOVING layer's RotationY-only restriction.
			// Constraint creation is in the same EnqueueCommand before any StepWorld, so no gravity fall.
			if (bIsDoor)
			{
				Params.bIsMovable = true;
				Params.PhysicsLayer = EPhysicsLayer::MOVING;
				Params.AllowedDOFs = 0x3F; // All DOFs
				Params.InitialVelocity = FVector::ZeroVector;
			}

			Params.Mesh = EffectiveRender->Mesh;
			Params.Material = EffectiveRender->MaterialOverride;
			Params.MeshScale = EffectiveRender->Scale;

			UE_LOG(LogFlecsEntity, Log, TEXT("SpawnEntity: NON-PROJ Key=%llu Mesh='%s' MatOverride='%s' RenderProfile=%p DefName='%s'"),
				static_cast<uint64>(EntityKey),
				Params.Mesh ? *Params.Mesh->GetName() : TEXT("NULL"),
				Params.Material ? *Params.Material->GetName() : TEXT("NULL"),
				EffectiveRender,
				Request.EntityDefinition ? *Request.EntityDefinition->EntityName.ToString() : TEXT("NoDefinition"));

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
				UE_LOG(LogFlecsEntity, Log, TEXT("SpawnEntity: PROJ Key=%llu Mesh='%s' MatOverride='%s' RenderProfile=%p DefName='%s'"),
					static_cast<uint64>(EntityKey),
					EffectiveRender->Mesh ? *EffectiveRender->Mesh->GetName() : TEXT("NULL"),
					EffectiveRender->MaterialOverride ? *EffectiveRender->MaterialOverride->GetName() : TEXT("NULL"),
					EffectiveRender,
					Request.EntityDefinition ? *Request.EntityDefinition->EntityName.ToString() : TEXT("NoDefinition"));

				FTransform RenderTransform;
				RenderTransform.SetLocation(Request.Location);
				RenderTransform.SetRotation(Request.Rotation.Quaternion() * EffectiveRender->RotationOffset.Quaternion());
				RenderTransform.SetScale3D(EffectiveRender->Scale);
				Renderer->AddInstance(EffectiveRender->Mesh, EffectiveRender->MaterialOverride, RenderTransform, EntityKey);
			}
		}

		// Register attached Niagara VFX (game thread, direct)
		if (EffectiveNiagara && EffectiveNiagara->HasAttachedEffect())
		{
			if (UFlecsNiagaraManager* NiagaraMgr = UFlecsNiagaraManager::Get(World))
			{
				NiagaraMgr->RegisterEntity(EntityKey, EffectiveNiagara->AttachedEffect,
					EffectiveNiagara->AttachedEffectScale, EffectiveNiagara->AttachedOffset);
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

	// Capture data for lambda.
	// Static component data (Health, Damage, Projectile, Container, Door) is NOT captured here —
	// it's inherited from the prefab via is_a() and read via Entity.try_get<T>() on sim thread.
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

		// Tags
		bool bPickupable;
		bool bDestructible;
		bool bHasLoot;
		bool bIsCharacter;
		bool bInteractable;

		// Owner
		FSkeletonKey OwnerKey;
		int64 OwnerEntityId;  // For projectiles (friendly fire check)

		// Focus camera override (per-instance)
		bool bOverrideFocusCamera;
		FVector FocusCameraPositionOverride;
		FRotator FocusCameraRotationOverride;

		// Interaction angle override (per-instance)
		bool bOverrideInteractionAngle;
		float InteractionAngleCosine;
		FVector InteractionAngleDirection;

		// Death VFX
		bool bHasDeathEffect;
		UNiagaraSystem* DeathEffect;
		float DeathEffectScale;
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

	// Instance data
	Data.ItemCount = ItemCount;
	Data.DespawnTime = DespawnTime;

	// Tags
	Data.bPickupable = bPickupable;
	Data.bDestructible = bDestructible;
	Data.bHasLoot = bHasLoot;
	Data.bIsCharacter = bIsCharacter;
	Data.bInteractable = bInteractable || EffectiveInteraction != nullptr;
	Data.OwnerKey = Request.OwnerKey;
	Data.OwnerEntityId = Request.OwnerEntityId;

	// Focus camera override
	Data.bOverrideFocusCamera = Request.bOverrideFocusCamera;
	Data.FocusCameraPositionOverride = Request.FocusCameraPositionOverride;
	Data.FocusCameraRotationOverride = Request.FocusCameraRotationOverride;

	// Interaction angle override (pre-compute cosine for fast dot-product check)
	Data.bOverrideInteractionAngle = Request.bOverrideInteractionAngle;
	if (Request.bOverrideInteractionAngle)
	{
		Data.InteractionAngleCosine = FMath::Cos(FMath::DegreesToRadians(Request.InteractionAngleOverride));
		FVector Dir = Request.InteractionDirectionOverride.GetSafeNormal();
		Data.InteractionAngleDirection = Dir.IsNearlyZero() ? FVector::ForwardVector : Dir;
	}
	else
	{
		Data.InteractionAngleCosine = -1.f;
		Data.InteractionAngleDirection = FVector::ForwardVector;
	}

	// Death VFX
	Data.bHasDeathEffect = EffectiveNiagara && EffectiveNiagara->HasDeathEffect();
	Data.DeathEffect = Data.bHasDeathEffect ? EffectiveNiagara->DeathEffect.Get() : nullptr;
	Data.DeathEffectScale = Data.bHasDeathEffect ? EffectiveNiagara->DeathEffectScale : 1.0f;

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
		// Static data comes from prefab via is_a() inheritance.
		// Read static components via try_get<T>() — works for prefab
		// entities (inherits via IsA) and standalone entities (returns nullptr).
		// ─────────────────────────────────────────────────────────

		// Health instance (read MaxHP from prefab's FHealthStatic)
		const FHealthStatic* HS = Entity.try_get<FHealthStatic>();
		if (HS)
		{
			FHealthInstance HealthInst;
			HealthInst.CurrentHP = HS->GetStartingHP();
			HealthInst.RegenAccumulator = 0.f;
			Entity.set<FHealthInstance>(HealthInst);

			// Register in sim→game state cache with initial health
			FlecsSubsystem->GetSimStateCache().Register(static_cast<int64>(Entity.id()));
			FlecsSubsystem->GetSimStateCache().WriteHealth(
				static_cast<int64>(Entity.id()), HealthInst.CurrentHP, HS->MaxHP);
		}

		// Projectile instance (read lifetime/grace from prefab's FProjectileStatic)
		const FProjectileStatic* PS = Entity.try_get<FProjectileStatic>();
		if (PS)
		{
			FProjectileInstance ProjInst;
			ProjInst.LifetimeRemaining = PS->MaxLifetime;
			ProjInst.BounceCount = 0;
			ProjInst.GraceFramesRemaining = PS->GracePeriodFrames;
			ProjInst.OwnerEntityId = Data.OwnerEntityId;
			Entity.set<FProjectileInstance>(ProjInst);
			Entity.add<FTagProjectile>();
		}

		// Item instance (check prefab's FItemStaticData)
		if (Entity.has<FItemStaticData>())
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

		// Container instance (read type/dimensions from prefab's FContainerStatic)
		const FContainerStatic* CS = Entity.try_get<FContainerStatic>();
		if (CS)
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
			switch (CS->Type)
			{
			case EContainerType::Grid:
				{
					FContainerGridInstance GridInst;
					GridInst.Initialize(CS->GridWidth, CS->GridHeight);
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
			Entity.add<FTagInteractable>();
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

		// Focus camera override (per-instance, read by character on interaction)
		if (Data.bOverrideFocusCamera)
		{
			FFocusCameraOverride CamOverride;
			CamOverride.CameraPosition = Data.FocusCameraPositionOverride;
			CamOverride.CameraRotation = Data.FocusCameraRotationOverride;
			Entity.set<FFocusCameraOverride>(CamOverride);
		}

		// Interaction angle override (per-instance, read during interaction trace)
		if (Data.bOverrideInteractionAngle)
		{
			FInteractionAngleOverride AngleOverride;
			AngleOverride.AngleCosine = Data.InteractionAngleCosine;
			AngleOverride.Direction = Data.InteractionAngleDirection;
			Entity.set<FInteractionAngleOverride>(AngleOverride);
		}

		// Death VFX component (read by DeadEntityCleanupSystem)
		if (Data.bHasDeathEffect && Data.DeathEffect)
		{
			FNiagaraDeathEffect DeathVFX;
			DeathVFX.Effect = Data.DeathEffect;
			DeathVFX.Scale = Data.DeathEffectScale;
			Entity.set<FNiagaraDeathEffect>(DeathVFX);
		}

		// ─────────────────────────────────────────────────────────
		// DOOR: set components, create constraint (reads FDoorStatic from prefab)
		// ─────────────────────────────────────────────────────────
		const FDoorStatic* DSPtr = Entity.try_get<FDoorStatic>();
		if (DSPtr && Data.bHasPhysics)
		{
			const FDoorStatic& DS = *DSPtr;
			Entity.add<FTagDoor>();

			FDoorInstance DoorInst;
			DoorInst.State = DS.bStartsLocked ? EDoorState::Locked : EDoorState::Closed;
			DoorInst.bUnlocked = !DS.bStartsLocked;

			FlecsSubsystem->EnsureBarrageAccess();

			// Use cached dispatch (safe against Deinitialize race, consistent with systems)
			UBarrageDispatch* Barrage = FlecsSubsystem->GetBarrageDispatch();
			checkf(Barrage, TEXT("SpawnEntity DOOR: BarrageDispatch is null!"));

			FBLet Prim = Barrage->GetShapeRef(Data.Key);
			checkf(FBarragePrimitive::IsNotNull(Prim), TEXT("SpawnEntity DOOR: Physics body not found for Key=%llu"), static_cast<uint64>(Data.Key));

			FBarrageKey DoorBarrageKey = Prim->KeyIntoBarrage;
			FBarrageKey WorldBody; // Invalid key -> Body::sFixedToWorld

			FBarrageConstraintSystem* ConstraintSystem = Barrage->GetConstraintSystem();
			checkf(ConstraintSystem, TEXT("SpawnEntity DOOR: ConstraintSystem is null!"));

			// Read body position for anchor computation (UE coordinates)
			FVector3f BodyPosF = FBarragePrimitive::GetPosition(Prim);
			FVector BodyPos(BodyPosF.X, BodyPosF.Y, BodyPosF.Z);
			FQuat BodyRot(FBarragePrimitive::OptimisticGetAbsoluteRotation(Prim));

			FBarrageConstraintKey CKey;

			if (DS.DoorType == EDoorType::Hinged)
			{
				FBHingeConstraintParams Params;
				Params.Body1 = DoorBarrageKey;
				Params.Body2 = WorldBody;
				Params.Space = EBConstraintSpace::WorldSpace;
				Params.bAutoDetectAnchor = false;

				// Compute hinge anchor in world space
				FVector WorldHingePos = BodyPos + BodyRot.RotateVector(DS.HingeOffset);
				Params.AnchorPoint1 = WorldHingePos;
				Params.AnchorPoint2 = WorldHingePos;

				// Hinge and normal axes (UE coordinates -- CreateHinge converts internally)
				Params.HingeAxis = DS.HingeAxis;
				FVector Up(0, 0, 1);
				if (FMath::Abs(FVector::DotProduct(Params.HingeAxis, Up)) > 0.99)
				{
					Up = FVector(1, 0, 0);
				}
				Params.NormalAxis = FVector::CrossProduct(Params.HingeAxis, Up).GetSafeNormal();

				// Angle limits
				Params.bHasLimits = true;
				if (DS.bBidirectional)
				{
					Params.MinAngle = -DS.MaxOpenAngle;
					Params.MaxAngle = DS.MaxOpenAngle;
				}
				else
				{
					Params.MinAngle = 0.f;
					Params.MaxAngle = DS.MaxOpenAngle;
				}

				// Motor settings -- bEnableMotor = false so CreateHinge doesn't activate
				// Velocity mode. Spring/friction/torque settings are still applied.
				// DoorTickSystem controls the motor state at runtime.
				Params.bEnableMotor = false;
				Params.MotorMaxTorque = DS.MotorMaxTorque;
				Params.MotorSpringFrequency = DS.MotorFrequency;
				Params.MotorSpringDamping = DS.MotorDamping;
				Params.MaxFrictionTorque = DS.FrictionTorque;

				// Hard limits (frequency=0 enables Baumgarte position correction, prevents bounce)
				Params.LimitSpringFrequency = 0.f;
				Params.LimitSpringDamping = 0.f;

				// Break thresholds (0 = unbreakable)
				Params.BreakForce = DS.ConstraintBreakForce;
				Params.BreakTorque = DS.ConstraintBreakTorque;

				CKey = ConstraintSystem->CreateHinge(Params);
			}
			else // Sliding
			{
				FBSliderConstraintParams Params;
				Params.Body1 = DoorBarrageKey;
				Params.Body2 = WorldBody;
				Params.Space = EBConstraintSpace::WorldSpace;
				Params.bAutoDetectAnchor = false;

				Params.AnchorPoint1 = BodyPos;
				Params.AnchorPoint2 = BodyPos;

				// Slider axis and perpendicular normal (UE coordinates)
				FVector SlideAxis = BodyRot.RotateVector(DS.SlideDirection).GetSafeNormal();
				Params.SliderAxis = SlideAxis;
				FVector Up(0, 0, 1);
				if (FMath::Abs(FVector::DotProduct(SlideAxis, Up)) > 0.99)
				{
					Up = FVector(1, 0, 0);
				}
				Params.NormalAxis = FVector::CrossProduct(SlideAxis, Up).GetSafeNormal();

				// Position limits (cm -- CreateSlider converts to Jolt meters internally)
				Params.bHasLimits = true;
				Params.MinLimit = 0.f;
				Params.MaxLimit = DS.SlideDistance;

				// Motor settings -- bEnableMotor = false, DoorTickSystem controls state
				Params.bEnableMotor = false;
				Params.MotorMaxForce = DS.MotorMaxTorque;
				Params.MotorSpringFrequency = DS.MotorFrequency;
				Params.MotorSpringDamping = DS.MotorDamping;
				Params.MaxFrictionForce = DS.FrictionTorque;

				// Hard limits (frequency=0 enables Baumgarte position correction, prevents bounce)
				Params.LimitSpringFrequency = 0.f;
				Params.LimitSpringDamping = 0.f;

				// Break thresholds (0 = unbreakable)
				Params.BreakForce = DS.ConstraintBreakForce;
				Params.BreakTorque = DS.ConstraintBreakTorque;

				CKey = ConstraintSystem->CreateSlider(Params);
			}

			checkf(CKey.IsValid(), TEXT("SpawnEntity DOOR: Failed to create %s constraint for Key=%llu!"),
				DS.DoorType == EDoorType::Hinged ? TEXT("Hinge") : TEXT("Slider"),
				static_cast<uint64>(Data.Key));

			DoorInst.ConstraintKey = CKey.Key;
			Entity.set<FDoorInstance>(DoorInst);
			Entity.add<FTagConstrained>();

			// Motor starts Off (bEnableMotor=false). If locked, hold at 0 with immovable motor.
			// Locked = total immunity to movement. Only way to move is break the constraint.
			if (DS.bStartsLocked)
			{
				ConstraintSystem->SetMotorTorqueLimits(CKey, 1e6f); // Immovable
				if (DS.DoorType == EDoorType::Hinged)
				{
					ConstraintSystem->SetMotorState(CKey, 2); // Position mode
					ConstraintSystem->SetTargetAngle(CKey, 0.f);
				}
				else
				{
					ConstraintSystem->SetMotorState(CKey, 2); // Position mode
					ConstraintSystem->SetTargetPosition(CKey, 0.f);
				}
			}

			UE_LOG(LogFlecsEntity, Log, TEXT("SpawnEntity DOOR: Created %s constraint Key=%lld for entity %llu"),
				DS.DoorType == EDoorType::Hinged ? TEXT("Hinge") : TEXT("Slider"),
				CKey.Key, Entity.id());

			// Body is already Dynamic/MOVING (created that way to avoid null MotionProperties crash).
			// Set mass and angular damping for proper door physics feel.
			Barrage->SetBodyMass(DoorBarrageKey, DS.Mass);
			Barrage->SetBodyAngularDamping(DoorBarrageKey, DS.AngularDamping);

			// Heavy mass at spawn: locked doors (reduces jitter from motor spring)
			// and latched doors (end position = closed at spawn).
			if (DS.bLockAtEndPosition || DS.bStartsLocked)
			{
				Barrage->SetBodyMass(DoorBarrageKey, DS.LockMass);
				UE_LOG(LogFlecsEntity, Log, TEXT("SpawnEntity DOOR: Applied LockMass=%.1f kg (locked=%d, latch=%d)"),
					DS.LockMass, DS.bStartsLocked, DS.bLockAtEndPosition);
			}
		}

		UE_LOG(LogFlecsEntity, Verbose, TEXT("Spawned entity: Key=%llu FlecsId=%llu Item=%d Health=%d Projectile=%d Container=%d Door=%d Prefab=%d"),
			static_cast<uint64>(Data.Key), Entity.id(),
			Entity.has<FItemStaticData>(), HS != nullptr, PS != nullptr, CS != nullptr, DSPtr != nullptr,
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

