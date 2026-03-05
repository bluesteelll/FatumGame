
#include "ItemRegistry.h"
#include "FlecsItemDefinition.h"
#include "FlecsContainerDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogItemRegistry, Log, All);

void UItemRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogItemRegistry, Log, TEXT("ItemRegistry initializing..."));

	ScanAndRegisterItems();
	ScanAndRegisterContainers();

	UE_LOG(LogItemRegistry, Log, TEXT("ItemRegistry initialized: %d items, %d containers"),
		ItemDefinitions.Num(), ContainerDefinitions.Num());
}

void UItemRegistry::Deinitialize()
{
	ItemDefinitions.Empty();
	ContainerDefinitions.Empty();
	ItemNameToId.Empty();
	ContainerNameToId.Empty();

	Super::Deinitialize();
}

void UItemRegistry::ScanAndRegisterItems()
{
	UAssetManager& AssetManager = UAssetManager::Get();

	// Get all assets of type FlecsItemDefinition
	TArray<FAssetData> AssetList;
	const FPrimaryAssetType ItemType("FlecsItemDefinition");

	AssetManager.GetPrimaryAssetDataList(ItemType, AssetList);

	if (AssetList.Num() == 0)
	{
		// Fallback: scan by class
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		FARFilter Filter;
		Filter.ClassPaths.Add(UFlecsItemDefinition::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;

		AssetRegistry.GetAssets(Filter, AssetList);
	}

	for (const FAssetData& AssetData : AssetList)
	{
		if (UFlecsItemDefinition* ItemDef = Cast<UFlecsItemDefinition>(AssetData.GetAsset()))
		{
			RegisterItem(ItemDef);
		}
	}
}

void UItemRegistry::ScanAndRegisterContainers()
{
	UAssetManager& AssetManager = UAssetManager::Get();

	TArray<FAssetData> AssetList;
	const FPrimaryAssetType ContainerType("FlecsContainerDefinition");

	AssetManager.GetPrimaryAssetDataList(ContainerType, AssetList);

	if (AssetList.Num() == 0)
	{
		// Fallback: scan by class
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		FARFilter Filter;
		Filter.ClassPaths.Add(UFlecsContainerDefinition::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;

		AssetRegistry.GetAssets(Filter, AssetList);
	}

	for (const FAssetData& AssetData : AssetList)
	{
		if (UFlecsContainerDefinition* ContainerDef = Cast<UFlecsContainerDefinition>(AssetData.GetAsset()))
		{
			RegisterContainer(ContainerDef);
		}
	}
}

void UItemRegistry::RegisterItem(UFlecsItemDefinition* Definition)
{
	if (!Definition)
	{
		UE_LOG(LogItemRegistry, Warning, TEXT("Attempted to register null item definition"));
		return;
	}

	if (Definition->ItemTypeId == 0)
	{
		UE_LOG(LogItemRegistry, Warning, TEXT("Item '%s' has invalid TypeId (0), skipping"),
			*Definition->ItemName.ToString());
		return;
	}

	if (ItemDefinitions.Contains(Definition->ItemTypeId))
	{
		UE_LOG(LogItemRegistry, Warning, TEXT("Duplicate item TypeId %u: '%s' conflicts with '%s'"),
			Definition->ItemTypeId,
			*Definition->ItemName.ToString(),
			*ItemDefinitions[Definition->ItemTypeId]->ItemName.ToString());
		return;
	}

	ItemDefinitions.Add(Definition->ItemTypeId, Definition);
	ItemNameToId.Add(Definition->ItemName, Definition->ItemTypeId);

	UE_LOG(LogItemRegistry, Verbose, TEXT("Registered item: %s (TypeId: %u)"),
		*Definition->ItemName.ToString(), Definition->ItemTypeId);
}

void UItemRegistry::RegisterContainer(UFlecsContainerDefinition* Definition)
{
	if (!Definition)
	{
		UE_LOG(LogItemRegistry, Warning, TEXT("Attempted to register null container definition"));
		return;
	}

	if (Definition->DefinitionId == 0)
	{
		UE_LOG(LogItemRegistry, Warning, TEXT("Container '%s' has invalid DefinitionId (0), skipping"),
			*Definition->ContainerName.ToString());
		return;
	}

	if (ContainerDefinitions.Contains(Definition->DefinitionId))
	{
		UE_LOG(LogItemRegistry, Warning, TEXT("Duplicate container DefinitionId %u: '%s' conflicts with '%s'"),
			Definition->DefinitionId,
			*Definition->ContainerName.ToString(),
			*ContainerDefinitions[Definition->DefinitionId]->ContainerName.ToString());
		return;
	}

	ContainerDefinitions.Add(Definition->DefinitionId, Definition);
	ContainerNameToId.Add(Definition->ContainerName, Definition->DefinitionId);

	UE_LOG(LogItemRegistry, Verbose, TEXT("Registered container: %s (DefinitionId: %u)"),
		*Definition->ContainerName.ToString(), Definition->DefinitionId);
}

void UItemRegistry::UnregisterItem(int32 TypeId)
{
	if (UFlecsItemDefinition* Def = ItemDefinitions.FindRef(TypeId))
	{
		ItemNameToId.Remove(Def->ItemName);
		ItemDefinitions.Remove(TypeId);
	}
}

void UItemRegistry::UnregisterContainer(int32 DefinitionId)
{
	if (UFlecsContainerDefinition* Def = ContainerDefinitions.FindRef(DefinitionId))
	{
		ContainerNameToId.Remove(Def->ContainerName);
		ContainerDefinitions.Remove(DefinitionId);
	}
}

UFlecsItemDefinition* UItemRegistry::GetItemDefinition(int32 TypeId) const
{
	return ItemDefinitions.FindRef(TypeId);
}

UFlecsContainerDefinition* UItemRegistry::GetContainerDefinition(int32 DefinitionId) const
{
	return ContainerDefinitions.FindRef(DefinitionId);
}

UFlecsItemDefinition* UItemRegistry::GetItemDefinitionByName(FName ItemName) const
{
	if (const int32* TypeId = ItemNameToId.Find(ItemName))
	{
		return ItemDefinitions.FindRef(*TypeId);
	}
	return nullptr;
}

UFlecsContainerDefinition* UItemRegistry::GetContainerDefinitionByName(FName ContainerName) const
{
	if (const int32* DefinitionId = ContainerNameToId.Find(ContainerName))
	{
		return ContainerDefinitions.FindRef(*DefinitionId);
	}
	return nullptr;
}

TArray<UFlecsItemDefinition*> UItemRegistry::GetAllItemDefinitions() const
{
	TArray<UFlecsItemDefinition*> Result;
	Result.Reserve(ItemDefinitions.Num());
	for (const auto& Pair : ItemDefinitions)
	{
		Result.Add(Pair.Value);
	}
	return Result;
}

TArray<UFlecsContainerDefinition*> UItemRegistry::GetAllContainerDefinitions() const
{
	TArray<UFlecsContainerDefinition*> Result;
	Result.Reserve(ContainerDefinitions.Num());
	for (const auto& Pair : ContainerDefinitions)
	{
		Result.Add(Pair.Value);
	}
	return Result;
}

UItemRegistry* UItemRegistry::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return nullptr;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		return nullptr;
	}

	return GameInstance->GetSubsystem<UItemRegistry>();
}
