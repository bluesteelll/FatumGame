// FlecsArtillerySubsystem - Item Prefab Registry

#include "FlecsArtillerySubsystem.h"
#include "FlecsComponents.h"
#include "FlecsItemDefinition.h"
#include "FlecsEntityDefinition.h"

// ═══════════════════════════════════════════════════════════════
// ITEM PREFAB REGISTRY IMPLEMENTATION
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
