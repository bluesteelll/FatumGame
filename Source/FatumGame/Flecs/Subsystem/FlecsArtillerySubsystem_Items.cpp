// FlecsArtillerySubsystem - Entity Prefab Registry

#include "FlecsArtillerySubsystem.h"
#include "FlecsStaticComponents.h"
#include "FlecsInstanceComponents.h"
#include "FlecsWeaponComponents.h"
#include "FlecsItemDefinition.h"
#include "FlecsEntityDefinition.h"
#include "FlecsHealthProfile.h"
#include "FlecsDamageProfile.h"
#include "FlecsProjectileProfile.h"
#include "FlecsContainerProfile.h"
#include "FlecsWeaponProfile.h"
#include "FlecsInteractionProfile.h"
#include "FlecsDestructibleProfile.h"
#include "FlecsDoorProfile.h"
#include "FlecsDoorComponents.h"
#include "FlecsMovementProfile.h"
#include "FlecsMovementStatic.h"

// ═══════════════════════════════════════════════════════════════
// ENTITY PREFAB REGISTRY IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════

flecs::entity UFlecsArtillerySubsystem::GetOrCreateEntityPrefab(UFlecsEntityDefinition* EntityDefinition)
{
	if (!FlecsWorld || !EntityDefinition)
	{
		return flecs::entity();
	}

	// Check if prefab already exists
	if (flecs::entity* Existing = EntityPrefabs.Find(EntityDefinition))
	{
		if (Existing->is_valid() && Existing->is_alive())
		{
			return *Existing;
		}
	}

	// Create new prefab
	FString PrefabName = FString::Printf(TEXT("EntityPrefab_%s"), *EntityDefinition->GetName());
	flecs::entity Prefab = FlecsWorld->prefab(TCHAR_TO_ANSI(*PrefabName));

	// Always add reference back to EntityDefinition
	FEntityDefinitionRef DefRef;
	DefRef.Definition = EntityDefinition;
	Prefab.set<FEntityDefinitionRef>(DefRef);

	// Add static components based on profiles
	if (UFlecsHealthProfile* HealthProfile = EntityDefinition->HealthProfile)
	{
		FHealthStatic HealthStatic;
		HealthStatic.MaxHP = HealthProfile->MaxHealth;
		HealthStatic.Armor = HealthProfile->Armor;
		HealthStatic.RegenPerSecond = HealthProfile->RegenPerSecond;
		HealthStatic.bDestroyOnDeath = HealthProfile->bDestroyOnDeath;
		// Calculate ratio from absolute StartingHealth value
		HealthStatic.StartingHPRatio = HealthProfile->StartingHealth > 0.f
			? HealthProfile->StartingHealth / HealthProfile->MaxHealth
			: 1.f;
		Prefab.set<FHealthStatic>(HealthStatic);
	}

	if (UFlecsDamageProfile* DamageProfile = EntityDefinition->DamageProfile)
	{
		FDamageStatic DamageStatic;
		DamageStatic.Damage = DamageProfile->Damage;
		DamageStatic.DamageType = DamageProfile->DamageType;
		DamageStatic.bAreaDamage = DamageProfile->bAreaDamage;
		DamageStatic.AreaRadius = DamageProfile->AreaRadius;
		DamageStatic.bDestroyOnHit = DamageProfile->bDestroyOnHit;
		DamageStatic.CritChance = DamageProfile->CriticalChance;
		DamageStatic.CritMultiplier = DamageProfile->CriticalMultiplier;
		Prefab.set<FDamageStatic>(DamageStatic);
	}

	if (UFlecsProjectileProfile* ProjProfile = EntityDefinition->ProjectileProfile)
	{
		FProjectileStatic ProjStatic;
		ProjStatic.MaxLifetime = ProjProfile->Lifetime;
		ProjStatic.MaxBounces = ProjProfile->MaxBounces;
		ProjStatic.GracePeriodFrames = ProjProfile->GetGraceFrames();
		ProjStatic.MinVelocity = ProjProfile->MinVelocity;
		ProjStatic.bMaintainSpeed = ProjProfile->bMaintainSpeed;
		ProjStatic.TargetSpeed = ProjProfile->DefaultSpeed;
		Prefab.set<FProjectileStatic>(ProjStatic);
	}

	if (UFlecsContainerProfile* ContainerProfile = EntityDefinition->ContainerProfile)
	{
		FContainerStatic ContainerStatic;
		ContainerStatic.Type = ContainerProfile->ContainerType;
		ContainerStatic.GridWidth = ContainerProfile->GridWidth;
		ContainerStatic.GridHeight = ContainerProfile->GridHeight;
		ContainerStatic.MaxItems = ContainerProfile->MaxListItems;
		ContainerStatic.MaxWeight = ContainerProfile->MaxWeight;
		ContainerStatic.bAllowNesting = ContainerProfile->bAllowNestedContainers;
		ContainerStatic.bAutoStack = ContainerProfile->bAutoStackOnAdd;
		Prefab.set<FContainerStatic>(ContainerStatic);
	}

	if (UFlecsItemDefinition* ItemDef = EntityDefinition->ItemDefinition)
	{
		FItemStaticData ItemStatic;
		ItemStatic.TypeId = ItemDef->ItemTypeId != 0 ? ItemDef->ItemTypeId : GetTypeHash(ItemDef->ItemName);
		ItemStatic.MaxStack = ItemDef->MaxStackSize;
		ItemStatic.Weight = ItemDef->Weight;
		ItemStatic.GridSize = ItemDef->GridSize;
		ItemStatic.ItemName = ItemDef->ItemName;
		ItemStatic.EntityDefinition = EntityDefinition;
		ItemStatic.ItemDefinition = ItemDef;
		Prefab.set<FItemStaticData>(ItemStatic);

		// Also register in ItemPrefabs for TypeId lookup
		ItemPrefabs.Add(ItemStatic.TypeId, Prefab);
	}

	if (UFlecsWeaponProfile* WeaponProfile = EntityDefinition->WeaponProfile)
	{
		FWeaponStatic WeaponStatic;

		// Firing
		WeaponStatic.ProjectileDefinition = WeaponProfile->ProjectileDefinition;
		WeaponStatic.FireInterval = WeaponProfile->GetFireInterval();
		WeaponStatic.BurstCount = WeaponProfile->BurstCount;
		WeaponStatic.BurstDelay = WeaponProfile->GetFireInterval() * 2.f; // Default: 2x fire interval
		WeaponStatic.ProjectileSpeedMultiplier = WeaponProfile->ProjectileSpeedMultiplier;
		WeaponStatic.DamageMultiplier = WeaponProfile->DamageMultiplier;
		WeaponStatic.ProjectilesPerShot = WeaponProfile->ProjectilesPerShot;
		WeaponStatic.bIsAutomatic = WeaponProfile->IsAutomatic();
		WeaponStatic.bIsBurst = WeaponProfile->IsBurst();

		// Ammo
		WeaponStatic.MagazineSize = WeaponProfile->MagazineSize;
		WeaponStatic.ReloadTime = WeaponProfile->ReloadTime;
		WeaponStatic.MaxReserveAmmo = WeaponProfile->MaxReserveAmmo;
		WeaponStatic.AmmoPerShot = WeaponProfile->AmmoPerShot;
		WeaponStatic.bUnlimitedAmmo = WeaponProfile->HasUnlimitedAmmo();

		// Muzzle
		WeaponStatic.MuzzleOffset = WeaponProfile->MuzzleOffset;
		WeaponStatic.MuzzleSocketName = WeaponProfile->MuzzleSocketName;

		// Visuals
		WeaponStatic.EquippedMesh = WeaponProfile->EquippedMesh;
		WeaponStatic.DroppedMesh = WeaponProfile->DroppedMesh;
		WeaponStatic.AttachSocket = WeaponProfile->AttachSocket;
		WeaponStatic.AttachOffset = WeaponProfile->AttachOffset;
		WeaponStatic.DroppedScale = WeaponProfile->DroppedScale;

		// Animations
		WeaponStatic.FireMontage = WeaponProfile->FireMontage;
		WeaponStatic.ReloadMontage = WeaponProfile->ReloadMontage;
		WeaponStatic.EquipMontage = WeaponProfile->EquipMontage;

		Prefab.set<FWeaponStatic>(WeaponStatic);
	}

	if (UFlecsInteractionProfile* InteractionProf = EntityDefinition->InteractionProfile)
	{
		FInteractionStatic InteractionStatic;
		InteractionStatic.MaxRange = InteractionProf->InteractionRange;
		InteractionStatic.bSingleUse = InteractionProf->bSingleUse;
		InteractionStatic.InteractionType = static_cast<uint8>(InteractionProf->InteractionType);
		InteractionStatic.InstantAction = static_cast<uint8>(InteractionProf->InstantAction);
		InteractionStatic.bRestrictAngle = InteractionProf->bRestrictAngle;
		if (InteractionProf->bRestrictAngle)
		{
			InteractionStatic.AngleCosine = FMath::Cos(FMath::DegreesToRadians(InteractionProf->InteractionAngle));
			FVector Dir = InteractionProf->InteractionDirection.GetSafeNormal();
			if (ensureMsgf(!Dir.IsNearlyZero(),
				TEXT("InteractionDirection is zero for '%s', falling back to ForwardVector"),
				*EntityDefinition->GetName()))
			{
				InteractionStatic.AngleDirection = Dir;
			}
			else
			{
				InteractionStatic.AngleDirection = FVector::ForwardVector;
			}
		}
		Prefab.set<FInteractionStatic>(InteractionStatic);
	}

	if (UFlecsDestructibleProfile* DestrProf = EntityDefinition->DestructibleProfile)
	{
		FDestructibleStatic DestrStatic;
		DestrStatic.Profile = DestrProf;
		Prefab.set<FDestructibleStatic>(DestrStatic);
	}

	if (UFlecsDoorProfile* DoorProf = EntityDefinition->DoorProfile)
	{
		FDoorStatic DoorStatic;
		DoorStatic.DoorType = DoorProf->IsHinged() ? EDoorType::Hinged : EDoorType::Sliding;
		DoorStatic.MaxOpenAngle = DoorProf->GetMaxOpenAngleRadians();
		DoorStatic.HingeAxis = DoorProf->HingeAxis.GetSafeNormal();
		DoorStatic.HingeOffset = DoorProf->HingeOffset;
		DoorStatic.bBidirectional = DoorProf->bBidirectional;
		DoorStatic.SlideDirection = DoorProf->SlideDirection.GetSafeNormal();
		DoorStatic.SlideDistance = DoorProf->SlideDistanceCm;
		DoorStatic.bMotorDriven = DoorProf->bMotorDriven;
		DoorStatic.MotorFrequency = DoorProf->MotorFrequency;
		DoorStatic.MotorDamping = DoorProf->MotorDamping;
		DoorStatic.MotorMaxTorque = DoorProf->MotorMaxForce;
		DoorStatic.FrictionTorque = DoorProf->FrictionForce;
		DoorStatic.bAutoClose = DoorProf->bAutoClose;
		DoorStatic.AutoCloseDelay = DoorProf->AutoCloseDelay;
		DoorStatic.bStartsLocked = DoorProf->bStartsLocked;
		DoorStatic.bUnlockOnInteraction = DoorProf->bUnlockOnInteraction;
		DoorStatic.bLockAtEndPosition = DoorProf->bLockAtEndPosition;
		DoorStatic.LockMass = DoorProf->LockMass;
		DoorStatic.ConstraintBreakForce = DoorProf->ConstraintBreakForce;
		DoorStatic.ConstraintBreakTorque = DoorProf->ConstraintBreakTorque;
		DoorStatic.Mass = DoorProf->Mass;
		DoorStatic.AngularDamping = DoorProf->AngularDamping;
		Prefab.set<FDoorStatic>(DoorStatic);
	}

	if (UFlecsMovementProfile* MoveProf = EntityDefinition->MovementProfile)
	{
		FMovementStatic MS;
		MS.WalkSpeed = MoveProf->WalkSpeed;
		MS.SprintSpeed = MoveProf->SprintSpeed;
		MS.CrouchSpeed = MoveProf->CrouchSpeed;
		MS.ProneSpeed = MoveProf->ProneSpeed;
		MS.GroundAcceleration = MoveProf->GroundAcceleration;
		MS.GroundDeceleration = MoveProf->GroundDeceleration;
		MS.AirAcceleration = MoveProf->AirAcceleration;
		MS.SprintAcceleration = MoveProf->SprintAcceleration;
		MS.JumpVelocity = MoveProf->JumpVelocity;
		MS.CrouchJumpVelocity = MoveProf->CrouchJumpVelocity;
		MS.GravityScale = MoveProf->GravityScale;
		MS.StandingHalfHeight = MoveProf->StandingHalfHeight;
		MS.StandingRadius = MoveProf->StandingRadius;
		MS.CrouchHalfHeight = MoveProf->CrouchHalfHeight;
		MS.CrouchRadius = MoveProf->CrouchRadius;
		MS.ProneHalfHeight = MoveProf->ProneHalfHeight;
		MS.ProneRadius = MoveProf->ProneRadius;
		MS.SlideMinEntrySpeed = MoveProf->SlideMinEntrySpeed;
		MS.SlideDeceleration = MoveProf->SlideDeceleration;
		MS.SlideMinExitSpeed = MoveProf->SlideMinExitSpeed;
		MS.SlideMaxDuration = MoveProf->SlideMaxDuration;
		MS.SlideInitialSpeedBoost = MoveProf->SlideInitialSpeedBoost;
		MS.SlideMinAcceleration = MoveProf->SlideMinAcceleration;
		Prefab.set<FMovementStatic>(MS);
	}

	// TODO: Add FLootStatic if needed

	// Store in registry
	EntityPrefabs.Add(EntityDefinition, Prefab);

	UE_LOG(LogTemp, Log, TEXT("Created entity prefab: '%s' (Health=%d, Damage=%d, Projectile=%d, Container=%d, Item=%d, Weapon=%d, Interaction=%d, Destructible=%d, Door=%d, Movement=%d)"),
		*EntityDefinition->GetName(),
		EntityDefinition->HealthProfile != nullptr,
		EntityDefinition->DamageProfile != nullptr,
		EntityDefinition->ProjectileProfile != nullptr,
		EntityDefinition->ContainerProfile != nullptr,
		EntityDefinition->ItemDefinition != nullptr,
		EntityDefinition->WeaponProfile != nullptr,
		EntityDefinition->InteractionProfile != nullptr,
		EntityDefinition->DestructibleProfile != nullptr,
		EntityDefinition->DoorProfile != nullptr,
		EntityDefinition->MovementProfile != nullptr);

	return Prefab;
}

// ═══════════════════════════════════════════════════════════════
// ITEM PREFAB REGISTRY IMPLEMENTATION (specialized for items)
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
