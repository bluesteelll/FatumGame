
#include "FlecsContainerLibrary.h"
#include "FlecsLibraryHelpers.h"
#include "FlecsStaticComponents.h"
#include "FlecsInstanceComponents.h"
#include "FlecsGameTags.h"
#include "FlecsEntityDefinition.h"
#include "FlecsItemDefinition.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlecsContainer, Log, All);

static flecs::entity ResolveContainerEntity(UFlecsArtillerySubsystem* Sub, int64 EntityId)
{
	if (EntityId == 0) return flecs::entity();
	flecs::world* World = Sub->GetFlecsWorld();
	if (!World) return flecs::entity();
	flecs::entity E = World->entity(static_cast<flecs::entity_t>(EntityId));
	if (!E.is_alive() || !E.has<FTagContainer>()) return flecs::entity();
	return E;
}

// ═══════════════════════════════════════════════════════════════
// ITEM OPERATIONS
// ═══════════════════════════════════════════════════════════════

void UFlecsContainerLibrary::SetItemDespawnTimer(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Timer)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !BarrageKey.IsValid()) return;

	FSkeletonKey CapturedKey = BarrageKey;
	Subsystem->EnqueueCommand([Subsystem, CapturedKey, Timer]()
	{
		flecs::entity Entity = FlecsLibrary::GetEntityForKey(Subsystem, CapturedKey);
		if (Entity.is_valid() && Entity.is_alive())
		{
			FWorldItemInstance* WorldItem = Entity.try_get_mut<FWorldItemInstance>();
			if (WorldItem)
			{
				WorldItem->DespawnTimer = Timer;
			}
		}
	});
}

// ═══════════════════════════════════════════════════════════════
// CONTAINER OPERATIONS
// ═══════════════════════════════════════════════════════════════

bool UFlecsContainerLibrary::AddItemToContainer(
	UObject* WorldContextObject,
	int64 ContainerEntityId,
	UFlecsEntityDefinition* EntityDefinition,
	int32 Count,
	int32& OutActuallyAdded)
{
	OutActuallyAdded = 0;

	if (ContainerEntityId == 0 || !EntityDefinition || Count <= 0)
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: Invalid parameters"));
		return false;
	}

	UFlecsItemDefinition* ItemDef = EntityDefinition->ItemDefinition;
	if (!ItemDef)
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: EntityDefinition '%s' has no ItemDefinition"),
			*EntityDefinition->GetName());
		return false;
	}

	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem)
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: No FlecsSubsystem"));
		return false;
	}

	UFlecsEntityDefinition* CapturedEntityDef = EntityDefinition;
	const FName ItemName = ItemDef->ItemName;

	Subsystem->EnqueueCommand([Subsystem, ContainerEntityId, CapturedEntityDef, ItemName, Count]()
	{
		flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		flecs::entity ContainerEntity = ResolveContainerEntity(Subsystem, ContainerEntityId);
		if (!ContainerEntity.is_valid())
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: Container %lld not found or not a container"), ContainerEntityId);
			return;
		}

		flecs::entity ItemPrefab = Subsystem->GetOrCreateItemPrefab(CapturedEntityDef);
		if (!ItemPrefab.is_valid())
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: Failed to create prefab for '%s'"),
				*ItemName.ToString());
			return;
		}

		const FContainerStatic* ContainerStatic = ContainerEntity.try_get<FContainerStatic>();
		if (!ContainerStatic)
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: Container %lld has no FContainerStatic"), ContainerEntityId);
			return;
		}

		FContainerInstance* ContainerInstance = ContainerEntity.try_get_mut<FContainerInstance>();
		if (!ContainerInstance)
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: Container %lld has no FContainerInstance"), ContainerEntityId);
			return;
		}

		auto CreateItemEntity = [&](FIntPoint GridPos, int32 SlotIdx) -> flecs::entity
		{
			FItemInstance Instance;
			Instance.Count = Count;

			FContainedIn Contained;
			Contained.ContainerEntityId = ContainerEntityId;
			Contained.GridPosition = GridPos;
			Contained.SlotIndex = SlotIdx;

			return FlecsWorld->entity()
				.is_a(ItemPrefab)
				.set<FItemInstance>(Instance)
				.set<FContainedIn>(Contained)
				.add<FTagItem>();
		};

		if (ContainerStatic->Type == EContainerType::List)
		{
			if (ContainerStatic->MaxItems > 0 && ContainerInstance->CurrentCount >= ContainerStatic->MaxItems)
			{
				UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: List container %lld is full"), ContainerEntityId);
				return;
			}

			flecs::entity ItemEntity = CreateItemEntity(FIntPoint(-1, -1), ContainerInstance->CurrentCount);
			ContainerInstance->CurrentCount++;

			UE_LOG(LogFlecsContainer, Log, TEXT("AddItemToContainer: Added '%s' (Count=%d) to container %lld. ItemEntity=%llu"),
				*ItemName.ToString(), Count, ContainerEntityId, ItemEntity.id());
		}
		else if (ContainerStatic->Type == EContainerType::Grid)
		{
			FContainerGridInstance* GridInstance = ContainerEntity.try_get_mut<FContainerGridInstance>();
			if (!GridInstance)
			{
				UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: Grid container %lld has no FContainerGridInstance"), ContainerEntityId);
				return;
			}

			const FItemStaticData* StaticData = ItemPrefab.try_get<FItemStaticData>();
			FIntPoint ItemSize = StaticData ? StaticData->GridSize : FIntPoint(1, 1);

			FIntPoint FreePos = GridInstance->FindFreeSpace(ItemSize, ContainerStatic->GridWidth, ContainerStatic->GridHeight);
			if (FreePos.X < 0)
			{
				UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: Grid container %lld is full"), ContainerEntityId);
				return;
			}

			GridInstance->Occupy(FreePos, ItemSize, ContainerStatic->GridWidth);
			flecs::entity ItemEntity = CreateItemEntity(FreePos, -1);
			ContainerInstance->CurrentCount++;

			UE_LOG(LogFlecsContainer, Log, TEXT("AddItemToContainer: Added '%s' to grid container %lld at (%d,%d). ItemEntity=%llu"),
				*ItemName.ToString(), ContainerEntityId, FreePos.X, FreePos.Y, ItemEntity.id());
		}
		else
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: Unsupported container type %d"),
				static_cast<int32>(ContainerStatic->Type));
		}
	});

	OutActuallyAdded = Count;
	return true;
}

bool UFlecsContainerLibrary::RemoveItemFromContainer(
	UObject* WorldContextObject,
	int64 ContainerEntityId,
	int64 ItemEntityId,
	int32 Count)
{
	if (ContainerEntityId == 0 || ItemEntityId == 0)
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("RemoveItemFromContainer: Invalid parameters"));
		return false;
	}

	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem) return false;

	Subsystem->EnqueueCommand([Subsystem, ContainerEntityId, ItemEntityId, Count]()
	{
		flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		flecs::entity ContainerEntity = ResolveContainerEntity(Subsystem, ContainerEntityId);
		if (!ContainerEntity.is_valid()) return;

		flecs::entity ItemEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(ItemEntityId));
		if (!ItemEntity.is_alive())
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("RemoveItemFromContainer: Item entity %lld not found"), ItemEntityId);
			return;
		}

		const FContainedIn* ContainedIn = ItemEntity.try_get<FContainedIn>();
		if (!ContainedIn || ContainedIn->ContainerEntityId != ContainerEntityId)
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("RemoveItemFromContainer: Item %lld not in container %lld"),
				ItemEntityId, ContainerEntityId);
			return;
		}

		const FContainerStatic* ContainerStatic = ContainerEntity.try_get<FContainerStatic>();
		FContainerInstance* ContainerInstance = ContainerEntity.try_get_mut<FContainerInstance>();

		if (ContainerStatic && ContainerStatic->Type == EContainerType::Grid && ContainedIn->IsInGrid())
		{
			if (FContainerGridInstance* GridInstance = ContainerEntity.try_get_mut<FContainerGridInstance>())
			{
				GridInstance->Free(ContainedIn->GridPosition, FIntPoint(1, 1), ContainerStatic->GridWidth);
			}
		}

		if (ContainerInstance)
		{
			ContainerInstance->CurrentCount = FMath::Max(0, ContainerInstance->CurrentCount - 1);
		}

		UE_LOG(LogFlecsContainer, Log, TEXT("RemoveItemFromContainer: Removed item %lld from container %lld"),
			ItemEntityId, ContainerEntityId);
		ItemEntity.destruct();
	});

	return true;
}

int32 UFlecsContainerLibrary::RemoveAllItemsFromContainer(
	UObject* WorldContextObject,
	int64 ContainerEntityId)
{
	if (ContainerEntityId == 0)
	{
		UE_LOG(LogFlecsContainer, Warning, TEXT("RemoveAllItemsFromContainer: Invalid ContainerEntityId"));
		return 0;
	}

	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem) return 0;

	Subsystem->EnqueueCommand([Subsystem, ContainerEntityId]()
	{
		flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		flecs::entity ContainerEntity = ResolveContainerEntity(Subsystem, ContainerEntityId);
		if (!ContainerEntity.is_valid())
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("RemoveAllItemsFromContainer: Container %lld not found"), ContainerEntityId);
			return;
		}

		TArray<flecs::entity> ItemsToRemove;
		FlecsWorld->each([ContainerEntityId, &ItemsToRemove](flecs::entity E, const FContainedIn& ContainedIn)
		{
			if (ContainedIn.ContainerEntityId == ContainerEntityId)
			{
				ItemsToRemove.Add(E);
			}
		});

		for (flecs::entity ItemEntity : ItemsToRemove)
		{
			ItemEntity.destruct();
		}

		const FContainerStatic* ContainerStatic = ContainerEntity.try_get<FContainerStatic>();
		FContainerInstance* ContainerInstance = ContainerEntity.try_get_mut<FContainerInstance>();

		if (ContainerInstance)
		{
			ContainerInstance->CurrentCount = 0;
		}

		if (ContainerStatic && ContainerStatic->Type == EContainerType::Grid)
		{
			if (FContainerGridInstance* GridInstance = ContainerEntity.try_get_mut<FContainerGridInstance>())
			{
				GridInstance->Initialize(ContainerStatic->GridWidth, ContainerStatic->GridHeight);
			}
		}

		UE_LOG(LogFlecsContainer, Log, TEXT("RemoveAllItemsFromContainer: Removed %d items from container %lld"),
			ItemsToRemove.Num(), ContainerEntityId);
	});

	return 0;
}

int32 UFlecsContainerLibrary::GetContainerItemCount(
	UObject* WorldContextObject,
	int64 ContainerEntityId)
{
	if (ContainerEntityId == 0) return -1;

	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !Subsystem->GetFlecsWorld()) return -1;

	flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
	flecs::entity ContainerEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(ContainerEntityId));
	if (!ContainerEntity.is_alive() || !ContainerEntity.has<FTagContainer>()) return -1;

	const FContainerInstance* ContainerInstance = ContainerEntity.try_get<FContainerInstance>();
	return ContainerInstance ? ContainerInstance->CurrentCount : 0;
}

bool UFlecsContainerLibrary::PickupItem(
	UObject* WorldContextObject,
	FSkeletonKey WorldItemKey,
	int64 ContainerEntityId,
	int32& OutPickedUp)
{
	OutPickedUp = 0;

	if (!WorldItemKey.IsValid() || ContainerEntityId == 0) return false;

	// TODO: Implement via simulation thread
	UE_LOG(LogFlecsContainer, Warning, TEXT("PickupItem: Not yet implemented"));
	return false;
}

FSkeletonKey UFlecsContainerLibrary::DropItem(
	UObject* WorldContextObject,
	int64 ContainerEntityId,
	int64 ItemEntityId,
	FVector DropLocation,
	int32 Count)
{
	if (ContainerEntityId == 0 || ItemEntityId == 0) return FSkeletonKey();

	// TODO: Implement via simulation thread
	UE_LOG(LogFlecsContainer, Warning, TEXT("DropItem: Not yet implemented"));
	return FSkeletonKey();
}
