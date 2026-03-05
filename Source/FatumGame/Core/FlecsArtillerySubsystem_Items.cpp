// FlecsArtillerySubsystem - Entity Prefab Registry

#include "FlecsArtillerySubsystem.h"
#include "FlecsHealthComponents.h"
#include "FlecsProjectileComponents.h"
#include "FlecsEntityComponents.h"
#include "FlecsItemComponents.h"
#include "FlecsDestructibleComponents.h"
#include "FlecsInteractionComponents.h"
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

	// Add static components based on profiles (FromProfile factory methods)
	if (EntityDefinition->HealthProfile)
		Prefab.set<FHealthStatic>(FHealthStatic::FromProfile(EntityDefinition->HealthProfile));

	if (EntityDefinition->DamageProfile)
		Prefab.set<FDamageStatic>(FDamageStatic::FromProfile(EntityDefinition->DamageProfile));

	if (EntityDefinition->ProjectileProfile)
		Prefab.set<FProjectileStatic>(FProjectileStatic::FromProfile(EntityDefinition->ProjectileProfile));

	if (EntityDefinition->ContainerProfile)
		Prefab.set<FContainerStatic>(FContainerStatic::FromProfile(EntityDefinition->ContainerProfile));

	if (UFlecsItemDefinition* ItemDef = EntityDefinition->ItemDefinition)
	{
		FItemStaticData ItemStatic = FItemStaticData::FromProfile(ItemDef, EntityDefinition);
		Prefab.set<FItemStaticData>(ItemStatic);
		ItemPrefabs.Add(ItemStatic.TypeId, Prefab);
	}

	if (EntityDefinition->WeaponProfile)
		Prefab.set<FWeaponStatic>(FWeaponStatic::FromProfile(EntityDefinition->WeaponProfile));

	if (EntityDefinition->InteractionProfile)
		Prefab.set<FInteractionStatic>(FInteractionStatic::FromProfile(EntityDefinition->InteractionProfile));

	if (UFlecsDestructibleProfile* DestrProf = EntityDefinition->DestructibleProfile)
	{
		FDestructibleStatic DestrStatic;
		DestrStatic.Profile = DestrProf;
		Prefab.set<FDestructibleStatic>(DestrStatic);
	}

	if (EntityDefinition->DoorProfile)
		Prefab.set<FDoorStatic>(FDoorStatic::FromProfile(EntityDefinition->DoorProfile));

	if (EntityDefinition->MovementProfile)
		Prefab.set<FMovementStatic>(FMovementStatic::FromProfile(EntityDefinition->MovementProfile));

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
	Prefab.set<FItemStaticData>(FItemStaticData::FromProfile(ItemDef, EntityDefinition));

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
