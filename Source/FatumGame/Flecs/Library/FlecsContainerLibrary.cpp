
#include "FlecsContainerLibrary.h"
#include "FlecsLibraryHelpers.h"
#include "FlecsStaticComponents.h"
#include "FlecsInstanceComponents.h"
#include "FlecsGameTags.h"
#include "FlecsEntityDefinition.h"
#include "FlecsItemDefinition.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlecsContainer, Log, All);

static void NotifyInventoryChanged(int64 ContainerEntityId)
{
	if (UFlecsMessageSubsystem::SelfPtr)
	{
		FUIInventoryChangedMessage Msg;
		Msg.ContainerEntityId = ContainerEntityId;
		UFlecsMessageSubsystem::SelfPtr->EnqueueMessage(TAG_UI_InventoryChanged, Msg);
	}
}

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
	int32& OutActuallyAdded,
	bool bAutoStack)
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

	Subsystem->EnqueueCommand([Subsystem, ContainerEntityId, CapturedEntityDef, ItemName, Count, bAutoStack]()
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

		const FItemStaticData* StaticData = ItemPrefab.try_get<FItemStaticData>();
		int32 Remaining = Count;

		// Auto-stack into existing items of the same type (controlled by bAutoStack parameter)
		if (bAutoStack && StaticData && StaticData->IsStackable())
		{
			FlecsWorld->each([ContainerEntityId, &ItemName, &Remaining, StaticData](
				flecs::entity E, const FContainedIn& Contained, FItemInstance& Inst)
			{
				if (Remaining <= 0) return;
				if (Contained.ContainerEntityId != ContainerEntityId) return;

				const FItemStaticData* ExistingStatic = E.try_get<FItemStaticData>();
				if (!ExistingStatic || ExistingStatic->ItemName != ItemName) return;
				if (Inst.Count >= ExistingStatic->MaxStack) return;

				const int32 Space = ExistingStatic->MaxStack - Inst.Count;
				const int32 ToTransfer = FMath::Min(Remaining, Space);
				Inst.Count += ToTransfer;
				Remaining -= ToTransfer;
			});

			if (Remaining <= 0)
			{
				UE_LOG(LogFlecsContainer, Log, TEXT("AddItemToContainer: Stacked all %d '%s' into existing items in container %lld"),
					Count, *ItemName.ToString(), ContainerEntityId);
				NotifyInventoryChanged(ContainerEntityId);
				return;
			}
		}

		auto CreateItemEntity = [&](FIntPoint GridPos, int32 SlotIdx, int32 ItemCount) -> flecs::entity
		{
			FItemInstance Instance;
			Instance.Count = ItemCount;

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

			CreateItemEntity(FIntPoint(-1, -1), ContainerInstance->CurrentCount, Remaining);
			ContainerInstance->CurrentCount++;

			UE_LOG(LogFlecsContainer, Log, TEXT("AddItemToContainer: Added '%s' (Count=%d) to container %lld"),
				*ItemName.ToString(), Remaining, ContainerEntityId);
		}
		else if (ContainerStatic->Type == EContainerType::Grid)
		{
			FContainerGridInstance* GridInstance = ContainerEntity.try_get_mut<FContainerGridInstance>();
			if (!GridInstance)
			{
				UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: Grid container %lld has no FContainerGridInstance"), ContainerEntityId);
				return;
			}

			FIntPoint ItemSize = StaticData ? StaticData->GridSize : FIntPoint(1, 1);

			FIntPoint FreePos = GridInstance->FindFreeSpace(ItemSize, ContainerStatic->GridWidth, ContainerStatic->GridHeight);
			if (FreePos.X < 0)
			{
				UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: Grid container %lld is full"), ContainerEntityId);
				return;
			}

			GridInstance->Occupy(FreePos, ItemSize, ContainerStatic->GridWidth);
			CreateItemEntity(FreePos, -1, Remaining);
			ContainerInstance->CurrentCount++;

			UE_LOG(LogFlecsContainer, Log, TEXT("AddItemToContainer: Added '%s' (Count=%d) to grid container %lld at (%d,%d)"),
				*ItemName.ToString(), Remaining, ContainerEntityId, FreePos.X, FreePos.Y);
		}
		else
		{
			UE_LOG(LogFlecsContainer, Warning, TEXT("AddItemToContainer: Unsupported container type %d"),
				static_cast<int32>(ContainerStatic->Type));
			return;
		}

		NotifyInventoryChanged(ContainerEntityId);
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
				const FItemStaticData* ItemStatic = ItemEntity.try_get<FItemStaticData>();
				FIntPoint ItemSize = ItemStatic ? ItemStatic->GridSize : FIntPoint(1, 1);
				GridInstance->Free(ContainedIn->GridPosition, ItemSize, ContainerStatic->GridWidth);
			}
		}

		if (ContainerInstance)
		{
			ContainerInstance->CurrentCount = FMath::Max(0, ContainerInstance->CurrentCount - 1);
		}

		UE_LOG(LogFlecsContainer, Log, TEXT("RemoveItemFromContainer: Removed item %lld from container %lld"),
			ItemEntityId, ContainerEntityId);
		ItemEntity.destruct();
		NotifyInventoryChanged(ContainerEntityId);
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
		NotifyInventoryChanged(ContainerEntityId);
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

// ═══════════════════════════════════════════════════════════════
// INVENTORY UI QUERIES
// ═══════════════════════════════════════════════════════════════

void UFlecsContainerLibrary::RequestContainerSnapshot(UObject* WorldContextObject, int64 ContainerEntityId)
{
	if (ContainerEntityId == 0) return;

	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem) return;

	Subsystem->EnqueueCommand([Subsystem, ContainerEntityId]()
	{
		flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		flecs::entity ContainerEntity = ResolveContainerEntity(Subsystem, ContainerEntityId);
		if (!ContainerEntity.is_valid()) return;

		const FContainerStatic* ContainerStatic = ContainerEntity.try_get<FContainerStatic>();
		const FContainerInstance* ContainerInstance = ContainerEntity.try_get<FContainerInstance>();
		if (!ContainerStatic || !ContainerInstance) return;

		FUIInventorySnapshotMessage Msg;
		Msg.ContainerEntityId = ContainerEntityId;
		Msg.GridWidth = ContainerStatic->GridWidth;
		Msg.GridHeight = ContainerStatic->GridHeight;
		Msg.MaxWeight = ContainerStatic->MaxWeight;
		Msg.CurrentWeight = ContainerInstance->CurrentWeight;
		Msg.CurrentCount = ContainerInstance->CurrentCount;

		FlecsWorld->each([ContainerEntityId, &Msg](flecs::entity E, const FContainedIn& ContainedIn)
		{
			if (ContainedIn.ContainerEntityId != ContainerEntityId) return;

			const FItemStaticData* StaticData = E.try_get<FItemStaticData>();
			const FItemInstance* ItemInst = E.try_get<FItemInstance>();

			FInventoryItemSnapshot Snap;
			Snap.ItemEntityId = static_cast<int64>(E.id());
			Snap.GridPosition = ContainedIn.GridPosition;
			Snap.GridSize = StaticData ? StaticData->GridSize : FIntPoint(1, 1);
			Snap.ItemName = StaticData ? StaticData->ItemName : NAME_None;
			Snap.Count = ItemInst ? ItemInst->Count : 1;
			Snap.MaxStack = StaticData ? StaticData->MaxStack : 99;
			Snap.Weight = StaticData ? StaticData->Weight : 0.f;
			Snap.ItemDefinition = StaticData ? StaticData->ItemDefinition : nullptr;
			if (Snap.ItemDefinition)
			{
				Snap.RarityTier = Snap.ItemDefinition->RarityTier;
			}

			Msg.Items.Add(MoveTemp(Snap));
		});

		if (UFlecsMessageSubsystem::SelfPtr)
		{
			UFlecsMessageSubsystem::SelfPtr->EnqueueMessage(TAG_UI_InventorySnapshot, Msg);
		}
	});
}

void UFlecsContainerLibrary::MoveItemInContainer(
	UObject* WorldContextObject,
	int64 ContainerEntityId,
	int64 ItemEntityId,
	FIntPoint NewGridPosition)
{
	if (ContainerEntityId == 0 || ItemEntityId == 0) return;

	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem) return;

	Subsystem->EnqueueCommand([Subsystem, ContainerEntityId, ItemEntityId, NewGridPosition]()
	{
		flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		flecs::entity ContainerEntity = ResolveContainerEntity(Subsystem, ContainerEntityId);
		if (!ContainerEntity.is_valid()) return;

		flecs::entity ItemEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(ItemEntityId));
		if (!ItemEntity.is_alive()) return;

		FContainedIn* ContainedIn = ItemEntity.try_get_mut<FContainedIn>();
		if (!ContainedIn || ContainedIn->ContainerEntityId != ContainerEntityId) return;

		const FContainerStatic* ContainerStatic = ContainerEntity.try_get<FContainerStatic>();
		if (!ContainerStatic || ContainerStatic->Type != EContainerType::Grid) return;

		FContainerGridInstance* GridInstance = ContainerEntity.try_get_mut<FContainerGridInstance>();
		if (!GridInstance) return;

		const FItemStaticData* SourceStatic = ItemEntity.try_get<FItemStaticData>();
		FIntPoint ItemSize = SourceStatic ? SourceStatic->GridSize : FIntPoint(1, 1);
		FIntPoint OldPosition = ContainedIn->GridPosition;

		// Free old position
		GridInstance->Free(OldPosition, ItemSize, ContainerStatic->GridWidth);

		// Try normal move first
		if (GridInstance->CanFit(NewGridPosition, ItemSize, ContainerStatic->GridWidth, ContainerStatic->GridHeight))
		{
			GridInstance->Occupy(NewGridPosition, ItemSize, ContainerStatic->GridWidth);
			ContainedIn->GridPosition = NewGridPosition;

			UE_LOG(LogFlecsContainer, Log, TEXT("MoveItemInContainer: Moved item %lld from (%d,%d) to (%d,%d)"),
				ItemEntityId, OldPosition.X, OldPosition.Y, NewGridPosition.X, NewGridPosition.Y);
			NotifyInventoryChanged(ContainerEntityId);
			return;
		}

		// Can't fit — try stacking onto item at target cell
		FItemInstance* SourceInst = ItemEntity.try_get_mut<FItemInstance>();
		if (SourceStatic && SourceStatic->IsStackable() && SourceInst)
		{
			// Find item that occupies the target cell
			flecs::entity TargetItem;
			const flecs::entity_t SourceId = static_cast<flecs::entity_t>(ItemEntityId);
			FlecsWorld->each([ContainerEntityId, NewGridPosition, SourceId, &TargetItem](
				flecs::entity E, const FContainedIn& C)
			{
				if (C.ContainerEntityId != ContainerEntityId || !C.IsInGrid() || E.id() == SourceId) return;
				const FItemStaticData* S = E.try_get<FItemStaticData>();
				FIntPoint Size = S ? S->GridSize : FIntPoint(1, 1);
				if (NewGridPosition.X >= C.GridPosition.X && NewGridPosition.X < C.GridPosition.X + Size.X
					&& NewGridPosition.Y >= C.GridPosition.Y && NewGridPosition.Y < C.GridPosition.Y + Size.Y)
				{
					TargetItem = E;
				}
			});

			if (TargetItem.is_valid())
			{
				const FItemStaticData* TargetStatic = TargetItem.try_get<FItemStaticData>();
				FItemInstance* TargetInst = TargetItem.try_get_mut<FItemInstance>();

				if (TargetStatic && TargetInst
					&& TargetStatic->ItemName == SourceStatic->ItemName
					&& TargetStatic->IsStackable()
					&& TargetInst->Count < TargetStatic->MaxStack)
				{
					const int32 SpaceInTarget = TargetStatic->MaxStack - TargetInst->Count;
					const int32 ToTransfer = FMath::Min(SourceInst->Count, SpaceInTarget);
					TargetInst->Count += ToTransfer;
					SourceInst->Count -= ToTransfer;

					if (SourceInst->Count <= 0)
					{
						// Fully merged — destroy source item
						FContainerInstance* ContainerInstance = ContainerEntity.try_get_mut<FContainerInstance>();
						if (ContainerInstance) ContainerInstance->CurrentCount--;
						ItemEntity.destruct();

						UE_LOG(LogFlecsContainer, Log, TEXT("MoveItemInContainer: Stacked item %lld into %llu (full merge, +%d)"),
							ItemEntityId, TargetItem.id(), ToTransfer);
					}
					else
					{
						// Partially merged — put source back at old position
						GridInstance->Occupy(OldPosition, ItemSize, ContainerStatic->GridWidth);

						UE_LOG(LogFlecsContainer, Log, TEXT("MoveItemInContainer: Stacked %d from item %lld into %llu (partial, %d remaining)"),
							ToTransfer, ItemEntityId, TargetItem.id(), SourceInst->Count);
					}

					NotifyInventoryChanged(ContainerEntityId);
					return;
				}
			}
		}

		// Neither move nor stack possible — rollback
		GridInstance->Occupy(OldPosition, ItemSize, ContainerStatic->GridWidth);
		UE_LOG(LogFlecsContainer, Log, TEXT("MoveItemInContainer: Cannot fit or stack item %lld at (%d,%d)"),
			ItemEntityId, NewGridPosition.X, NewGridPosition.Y);
	});
}

bool UFlecsContainerLibrary::GetContainerInfo(
	UObject* WorldContextObject,
	int64 ContainerEntityId,
	int32& OutGridWidth,
	int32& OutGridHeight,
	float& OutMaxWeight,
	float& OutCurrentWeight,
	int32& OutCurrentCount)
{
	OutGridWidth = 0;
	OutGridHeight = 0;
	OutMaxWeight = -1.f;
	OutCurrentWeight = 0.f;
	OutCurrentCount = 0;

	if (ContainerEntityId == 0) return false;

	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !Subsystem->GetFlecsWorld()) return false;

	flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
	flecs::entity ContainerEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(ContainerEntityId));
	if (!ContainerEntity.is_alive() || !ContainerEntity.has<FTagContainer>()) return false;

	const FContainerStatic* ContainerStatic = ContainerEntity.try_get<FContainerStatic>();
	const FContainerInstance* ContainerInstance = ContainerEntity.try_get<FContainerInstance>();
	if (!ContainerStatic) return false;

	OutGridWidth = ContainerStatic->GridWidth;
	OutGridHeight = ContainerStatic->GridHeight;
	OutMaxWeight = ContainerStatic->MaxWeight;
	if (ContainerInstance)
	{
		OutCurrentWeight = ContainerInstance->CurrentWeight;
		OutCurrentCount = ContainerInstance->CurrentCount;
	}
	return true;
}
