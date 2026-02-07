
#include "FlecsSpawnLibrary.h"
#include "FlecsConstraintLibrary.h"
#include "FlecsEntitySpawner.h"
#include "FlecsLibraryHelpers.h"
#include "FlecsGameTags.h"
#include "FlecsStaticComponents.h"
#include "FlecsInstanceComponents.h"
#include "FlecsBarrageComponents.h"
#include "FlecsProjectileDefinition.h"
#include "FlecsEntityDefinition.h"
#include "FlecsPhysicsProfile.h"
#include "FlecsRenderProfile.h"
#include "FlecsDamageProfile.h"
#include "FlecsProjectileProfile.h"
#include "FlecsConstrainedGroupDefinition.h"
#include "BarrageSpawnUtils.h"
#include "BarrageConstraintSystem.h"
#include "FlecsRenderManager.h"
#include "FBarragePrimitive.h"
#include "FBShapeParams.h"
#include "Skeletonize.h"
#include "Engine/StaticMesh.h"
#include <atomic>

namespace
{
	std::atomic<uint32> GProjectileCounter{0};
	std::atomic<uint32> GGroupElementCounter{0};
}

// ═══════════════════════════════════════════════════════════════
// SPAWN (game-thread safe)
// ═══════════════════════════════════════════════════════════════

void UFlecsSpawnLibrary::SpawnWorldItem(
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

	FBarrageSpawnParams Params;
	Params.Mesh = Mesh;
	Params.WorldTransform = FTransform(Location);
	Params.PhysicsLayer = PhysicsLayer;
	Params.bAutoCollider = true;
	Params.bIsMovable = true;

	FBarrageSpawnResult Result = FBarrageSpawnUtils::SpawnEntity(World, Params);
	if (!Result.bSuccess) return;

	FSkeletonKey EntityKey = Result.EntityKey;
	Subsystem->EnqueueCommand([Subsystem, EntityKey, Mesh, Count, DespawnTime]()
	{
		flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		FItemStaticData ItemStatic;
		ItemStatic.TypeId = 0;
		ItemStatic.MaxStack = 99;
		ItemStatic.Weight = 0.1f;
		ItemStatic.GridSize = FIntPoint(1, 1);

		FItemInstance ItemInstance;
		ItemInstance.Count = Count;

		FWorldItemInstance WorldItem;
		WorldItem.DespawnTimer = DespawnTime;
		WorldItem.PickupGraceTimer = 0.f;

		flecs::entity Entity = FlecsWorld->entity()
			.set<FItemStaticData>(ItemStatic)
			.set<FItemInstance>(ItemInstance)
			.set<FWorldItemInstance>(WorldItem)
			.set<FISMRender>({ Mesh, FVector::OneVector })
			.add<FTagItem>()
			.add<FTagPickupable>();

		Subsystem->BindEntityToBarrage(Entity, EntityKey);
	});
}

void UFlecsSpawnLibrary::SpawnDestructible(
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

		FHealthStatic HealthStatic;
		HealthStatic.MaxHP = MaxHP;
		HealthStatic.Armor = 0.f;
		HealthStatic.bDestroyOnDeath = true;

		FHealthInstance HealthInstance;
		HealthInstance.CurrentHP = MaxHP;

		flecs::entity Entity = FlecsWorld->entity()
			.set<FHealthStatic>(HealthStatic)
			.set<FHealthInstance>(HealthInstance)
			.set<FISMRender>({ Mesh, FVector::OneVector })
			.add<FTagDestructible>();

		Subsystem->BindEntityToBarrage(Entity, EntityKey);
	});
}

void UFlecsSpawnLibrary::SpawnLootableDestructible(
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

		FHealthStatic HealthStatic;
		HealthStatic.MaxHP = MaxHP;
		HealthStatic.Armor = 0.f;
		HealthStatic.bDestroyOnDeath = true;

		FHealthInstance HealthInstance;
		HealthInstance.CurrentHP = MaxHP;

		FLootStatic LootStatic;
		LootStatic.MinDrops = MinDrops;
		LootStatic.MaxDrops = MaxDrops;
		LootStatic.DropChance = 1.f;

		flecs::entity Entity = FlecsWorld->entity()
			.set<FHealthStatic>(HealthStatic)
			.set<FHealthInstance>(HealthInstance)
			.set<FLootStatic>(LootStatic)
			.set<FISMRender>({ Mesh, FVector::OneVector })
			.add<FTagDestructible>()
			.add<FTagHasLoot>();

		Subsystem->BindEntityToBarrage(Entity, EntityKey);
	});
}

// ═══════════════════════════════════════════════════════════════
// PROJECTILE SPAWNING
// ═══════════════════════════════════════════════════════════════

FSkeletonKey UFlecsSpawnLibrary::SpawnProjectileFromDefinition(
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

FSkeletonKey UFlecsSpawnLibrary::SpawnProjectileFromEntityDef(
	UObject* WorldContextObject,
	UFlecsEntityDefinition* Definition,
	FVector Location,
	FVector Direction,
	float SpeedOverride,
	int64 OwnerEntityId)
{
	if (!Definition)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpawnProjectileFromEntityDef: Definition is null!"));
		return FSkeletonKey();
	}

	if (!Definition->ProjectileProfile)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpawnProjectileFromEntityDef: '%s' has no ProjectileProfile!"), *Definition->GetName());
		return FSkeletonKey();
	}

	if (!Definition->RenderProfile || !Definition->RenderProfile->Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpawnProjectileFromEntityDef: '%s' has no RenderProfile or mesh!"), *Definition->GetName());
		return FSkeletonKey();
	}

	const float Speed = SpeedOverride > 0.f ? SpeedOverride : Definition->ProjectileProfile->DefaultSpeed;
	FVector NormalizedDir = Direction.GetSafeNormal();
	FVector Velocity = NormalizedDir * Speed;

	FEntitySpawnRequest Request = FEntitySpawnRequest::FromDefinition(Definition, Location)
		.WithVelocity(Velocity)
		.WithOwnerEntity(OwnerEntityId);

	return UFlecsEntityLibrary::SpawnEntity(WorldContextObject, Request);
}

FSkeletonKey UFlecsSpawnLibrary::SpawnProjectile(
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
	UFlecsRenderManager* Renderer = UFlecsRenderManager::Get(World);
	UFlecsArtillerySubsystem* Subsystem = World->GetSubsystem<UFlecsArtillerySubsystem>();

	if (!Barrage)
	{
		UE_LOG(LogTemp, Error, TEXT("PROJ_DEBUG SpawnProjectile: No UBarrageDispatch!"));
		return FSkeletonKey();
	}

	const uint32 Id = ++GProjectileCounter;
	FSkeletonKey EntityKey = FSkeletonKey(FORGE_SKELETON_KEY(Id, SKELLY::SFIX_GUN_SHOT));

	const bool bNeedsDynamicBody = bIsBouncing || GravityFactor > 0.f;

	UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG SpawnProjectile START: Key=%llu Counter=%u Loc=%s Bouncing=%d Gravity=%.2f Dynamic=%d"),
		static_cast<uint64>(EntityKey), Id, *Location.ToString(), bIsBouncing, GravityFactor, bNeedsDynamicBody);

	Direction.Normalize();
	FVector Velocity = Direction * Speed;

	FBSphereParams SphereParams = FBarrageBounder::GenerateSphereBounds(Location, CollisionRadius);

	FBLet Body;
	if (bNeedsDynamicBody)
	{
		Body = Barrage->CreateBouncingSphere(
			SphereParams,
			EntityKey,
			static_cast<uint16>(EPhysicsLayer::PROJECTILE),
			bIsBouncing ? Restitution : 0.f,
			Friction
		);
		UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG CreateBouncingSphere: Key=%llu Body=%p"),
			static_cast<uint64>(EntityKey), Body.Get());
	}
	else
	{
		Body = Barrage->CreatePrimitive(
			SphereParams,
			EntityKey,
			static_cast<uint16>(EPhysicsLayer::PROJECTILE),
			true
		);
		UE_LOG(LogTemp, Log, TEXT("PROJ_DEBUG CreatePrimitive (sensor): Key=%llu Body=%p"),
			static_cast<uint64>(EntityKey), Body.Get());
	}

	if (!FBarragePrimitive::IsNotNull(Body))
	{
		UE_LOG(LogTemp, Error, TEXT("PROJ_DEBUG SpawnProjectile: FAILED to create physics body! Key=%llu"),
			static_cast<uint64>(EntityKey));
		return FSkeletonKey();
	}

	FBarragePrimitive::SetVelocity(Velocity, Body);
	FBarragePrimitive::SetGravityFactor(GravityFactor, Body);

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

			FDamageStatic DamageStatic;
			DamageStatic.Damage = Damage;
			DamageStatic.bAreaDamage = false;
			DamageStatic.bDestroyOnHit = (MaxBounces != -1);

			FProjectileStatic ProjStatic;
			ProjStatic.MaxLifetime = LifetimeSeconds;
			ProjStatic.MaxBounces = MaxBounces;
			ProjStatic.GracePeriodFrames = 30;
			ProjStatic.MinVelocity = 50.f;

			FProjectileInstance ProjInstance;
			ProjInstance.LifetimeRemaining = LifetimeSeconds;
			ProjInstance.BounceCount = 0;
			ProjInstance.GraceFramesRemaining = 30;

			flecs::entity Entity = FlecsWorld->entity()
				.set<FDamageStatic>(DamageStatic)
				.set<FProjectileStatic>(ProjStatic)
				.set<FProjectileInstance>(ProjInstance)
				.set<FISMRender>({ Mesh, FVector(VisualScale) })
				.add<FTagProjectile>();

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
// CONSTRAINED GROUP SPAWNING
// ═══════════════════════════════════════════════════════════════

FFlecsGroupSpawnResult UFlecsSpawnLibrary::SpawnConstrainedGroup(
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
	UFlecsRenderManager* Renderer = UFlecsRenderManager::Get(World);
	UFlecsArtillerySubsystem* FlecsSubsystem = World->GetSubsystem<UFlecsArtillerySubsystem>();

	if (!Barrage || !FlecsSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("SpawnConstrainedGroup: Missing subsystems!"));
		return Result;
	}

	FTransform GroupTransform(Rotation, Location);
	Result.ElementKeys.Reserve(Definition->Elements.Num());
	Result.ConstraintKeys.Reserve(Definition->Constraints.Num());

	// PHASE 1: Spawn all elements
	for (const FFlecsGroupElement& Element : Definition->Elements)
	{
		if (!Element.Mesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("SpawnConstrainedGroup: Element '%s' has no mesh!"), *Element.ElementName.ToString());
			Result.ElementKeys.Add(FSkeletonKey());
			continue;
		}

		FTransform LocalTransform(Element.LocalRotation, Element.LocalOffset, Element.Scale);
		FTransform WorldTransform = LocalTransform * GroupTransform;

		const uint32 Id = ++GGroupElementCounter;
		FSkeletonKey EntityKey = FSkeletonKey(FORGE_SKELETON_KEY(Id, SKELLY::SFIX_ITEM));

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

		if (Renderer)
		{
			Renderer->AddInstance(Element.Mesh, Element.Material, WorldTransform, EntityKey);
		}

		Result.ElementKeys.Add(EntityKey);

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
				FHealthStatic HealthStatic;
				HealthStatic.MaxHP = MaxHP;
				HealthStatic.Armor = ArmorVal;
				HealthStatic.bDestroyOnDeath = true;

				FHealthInstance HealthInstance;
				HealthInstance.CurrentHP = MaxHP;

				Entity.set<FHealthStatic>(HealthStatic);
				Entity.set<FHealthInstance>(HealthInstance);
				Entity.add<FTagDestructible>();
			}

			FlecsSubsystem->BindEntityToBarrage(Entity, EntityKey);
		});
	}

	// PHASE 2: Create constraints between elements
	for (const FFlecsGroupConstraint& ConstraintDef : Definition->Constraints)
	{
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

FFlecsGroupSpawnResult UFlecsSpawnLibrary::SpawnChain(
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
	UFlecsRenderManager* Renderer = UFlecsRenderManager::Get(World);
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

		FlecsSubsystem->EnqueueCommand([FlecsSubsystem, EntityKey, Mesh, MaxHealth]()
		{
			flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
			if (!FlecsWorld) return;

			flecs::entity Entity = FlecsWorld->entity()
				.set<FISMRender>({ Mesh, FVector::OneVector });

			if (MaxHealth > 0.f)
			{
				FHealthStatic HealthStatic;
				HealthStatic.MaxHP = MaxHealth;
				HealthStatic.Armor = 0.f;
				HealthStatic.bDestroyOnDeath = true;

				FHealthInstance HealthInstance;
				HealthInstance.CurrentHP = MaxHealth;

				Entity.set<FHealthStatic>(HealthStatic);
				Entity.set<FHealthInstance>(HealthInstance);
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

		int64 ConstraintKey = UFlecsConstraintLibrary::CreateFixedConstraint(WorldContextObject, Key1, Key2, BreakForce, 0.f);
		Result.ConstraintKeys.Add(ConstraintKey);
	}

	Result.bSuccess = true;
	return Result;
}
