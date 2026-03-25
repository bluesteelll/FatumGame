// UFlecsUISubsystem — bridge between FlecsUI models and sim thread.

#include "FlecsUISubsystem.h"
#include "FlecsContainerModel.h"
#include "FlecsValueModel.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsItemComponents.h"
#include "FlecsItemDefinition.h"
#include "FlecsGameTags.h"
#include "FSimStateCache.h"
#include "flecs.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlecsUI, Log, All);

// ═══════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════

void UFlecsUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UFlecsArtillerySubsystem>();
	Super::Initialize(Collection);

	SelfPtr = this;
	Artillery = Cast<UFlecsArtillerySubsystem>(
		GetWorld()->GetSubsystemBase(UFlecsArtillerySubsystem::StaticClass()));
}

void UFlecsUISubsystem::Deinitialize()
{
	SelfPtr = nullptr;
	Containers.Empty();
	Values.Empty();
	GCRoots.Empty();
	Super::Deinitialize();
}

bool UFlecsUISubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UFlecsUISubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFlecsUISubsystem, STATGROUP_Tickables);
}

// ═══════════════════════════════════════════════════════════════
// MODEL FACTORY
// ═══════════════════════════════════════════════════════════════

UFlecsContainerModel* UFlecsUISubsystem::AcquireContainerModel(int64 ContainerEntityId)
{
	check(ContainerEntityId != 0);

	FContainerEntry* Existing = Containers.Find(ContainerEntityId);
	if (Existing)
	{
		Existing->RefCount++;
		return Existing->Model;
	}

	FContainerEntry& Entry = Containers.Add(ContainerEntityId);
	Entry.Model = NewObject<UFlecsContainerModel>(this);
	Entry.SharedState = MakeUnique<FContainerSharedState>();
	Entry.RefCount = 1;
	GCRoots.Add(Entry.Model);

	SetupContainerBridge(ContainerEntityId);

	// Request initial snapshot
	if (Artillery)
	{
		auto* SharedState = Entry.SharedState.Get();
		Artillery->EnqueueCommand([this, ContainerEntityId, SharedState]()
		{
			FContainerSnapshot Snap = BuildContainerSnapshot(ContainerEntityId);
			SharedState->SnapshotBuffer.WriteAndSwap(MoveTemp(Snap));
			SharedState->SimVersion.fetch_add(1, std::memory_order_release);
		});
	}

	return Entry.Model;
}

UFlecsValueModel* UFlecsUISubsystem::AcquireValueModel(int64 EntityId)
{
	check(EntityId != 0);

	FValueEntry* Existing = Values.Find(EntityId);
	if (Existing)
	{
		Existing->RefCount++;
		return Existing->Model;
	}

	FValueEntry& Entry = Values.Add(EntityId);
	Entry.Model = NewObject<UFlecsValueModel>(this);
	Entry.RefCount = 1;
	GCRoots.Add(Entry.Model);

	SetupValueBridge(EntityId);

	return Entry.Model;
}

void UFlecsUISubsystem::ReleaseModel(int64 EntityId)
{
	// Check containers
	if (FContainerEntry* Entry = Containers.Find(EntityId))
	{
		Entry->RefCount--;
		if (Entry->RefCount <= 0)
		{
			GCRoots.Remove(Entry->Model);
			Entry->Model->Deactivate();
			Containers.Remove(EntityId);
		}
		return;
	}

	// Check values
	if (FValueEntry* Entry = Values.Find(EntityId))
	{
		Entry->RefCount--;
		if (Entry->RefCount <= 0)
		{
			GCRoots.Remove(Entry->Model);
			Entry->Model->Deactivate();
			Values.Remove(EntityId);
		}
	}
}

FContainerSharedState* UFlecsUISubsystem::FindContainerSharedState(int64 ContainerEntityId)
{
	FContainerEntry* Entry = Containers.Find(ContainerEntityId);
	return Entry ? Entry->SharedState.Get() : nullptr;
}

// ═══════════════════════════════════════════════════════════════
// TICK — O(N) atomic loads, zero sim interaction
// ═══════════════════════════════════════════════════════════════

void UFlecsUISubsystem::Tick(float DeltaTime)
{
	// ═══ Container models: check version + read triple buffer ═══
	for (auto& [Id, Entry] : Containers)
	{
		auto& S = *Entry.SharedState;

		// 1 atomic load — ~1ns
		const uint32 V = S.SimVersion.load(std::memory_order_acquire);
		if (V != S.GameSeenVersion)
		{
			S.GameSeenVersion = V;
			S.SnapshotBuffer.SwapReadBuffers();
			Entry.Model->ReceiveSnapshot(S.SnapshotBuffer.Read());
		}

		// Drain op results
		FOpResult R;
		while (S.OpResults.Dequeue(R))
		{
			Entry.Model->ReceiveOpResult(R.OpId, R.Result);
		}
	}

	// ═══ Value models: read from FSimStateCache ═══
	if (Artillery)
	{
		static const FName NAME_CurrentHP("CurrentHP");
		static const FName NAME_MaxHP("MaxHP");
		static const FName NAME_CurrentAmmo("CurrentAmmo");
		static const FName NAME_MagazineSize("MagazineSize");
		static const FName NAME_ReserveAmmo("ReserveAmmo");

		const FSimStateCache& Cache = Artillery->GetSimStateCache();
		for (auto& [Id, Entry] : Values)
		{
			// Health
			FHealthSnapshot HealthSnap;
			if (Cache.ReadHealth(Id, HealthSnap))
			{
				Entry.Model->UpdateFloat(NAME_CurrentHP, HealthSnap.CurrentHP);
				Entry.Model->UpdateFloat(NAME_MaxHP, HealthSnap.MaxHP);
			}

			// Weapon
			FWeaponSnapshot WeaponSnap;
			if (Cache.ReadWeapon(Id, WeaponSnap))
			{
				Entry.Model->UpdateInt(NAME_CurrentAmmo, WeaponSnap.CurrentAmmo);
				Entry.Model->UpdateInt(NAME_MagazineSize, WeaponSnap.MagazineSize);
				Entry.Model->UpdateInt(NAME_ReserveAmmo, WeaponSnap.ReserveAmmo);
			}
		}
	}
}

// ═══════════════════════════════════════════════════════════════
// BRIDGE SETUP
// ═══════════════════════════════════════════════════════════════

void UFlecsUISubsystem::SetupContainerBridge(int64 ContainerId)
{
	FContainerEntry* Entry = Containers.Find(ContainerId);
	if (!Entry) return;

	auto* SharedState = Entry->SharedState.Get();

	// MoveItemOnSim: actual sim-thread mutation + snapshot + result
	Entry->Model->MoveItemOnSim = [this, ContainerId, SharedState](uint32 OpId, int64 ItemEntityId, FIntPoint NewPos)
	{
		if (!Artillery) return;

		Artillery->EnqueueCommand([this, ContainerId, OpId, SharedState, ItemEntityId, NewPos]()
		{
			EUIOpResult Result = EUIOpResult::Failed;

			flecs::world* FlecsWorld = Artillery->GetFlecsWorld();
			if (FlecsWorld)
			{
				flecs::entity ContainerEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(ContainerId));
				flecs::entity ItemEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(ItemEntityId));

				if (ContainerEntity.is_alive() && ItemEntity.is_alive())
				{
					FContainedIn* ContainedIn = ItemEntity.try_get_mut<FContainedIn>();
					const FContainerStatic* ContainerStatic = ContainerEntity.try_get<FContainerStatic>();
					FContainerGridInstance* GridInstance = ContainerEntity.try_get_mut<FContainerGridInstance>();

					if (ContainedIn && ContainedIn->ContainerEntityId == ContainerId
						&& ContainerStatic && ContainerStatic->Type == EContainerType::Grid
						&& GridInstance)
					{
						const FItemStaticData* SourceStatic = ItemEntity.try_get<FItemStaticData>();
						const FIntPoint ItemSize = SourceStatic ? SourceStatic->GridSize : FIntPoint(1, 1);
						const FIntPoint OldPos = ContainedIn->GridPosition;

						// Free old position
						GridInstance->Free(OldPos, ItemSize, ContainerStatic->GridWidth);

						// Try normal move
						if (GridInstance->CanFit(NewPos, ItemSize, ContainerStatic->GridWidth, ContainerStatic->GridHeight))
						{
							GridInstance->Occupy(NewPos, ItemSize, ContainerStatic->GridWidth);
							ContainedIn->GridPosition = NewPos;
							Result = EUIOpResult::Success;
						}
						else
						{
							// Try stacking onto item at target cell
							FItemInstance* SourceInst = ItemEntity.try_get_mut<FItemInstance>();
							if (SourceStatic && SourceStatic->IsStackable() && SourceInst)
							{
								flecs::entity TargetItem;
								const flecs::entity_t SourceId = static_cast<flecs::entity_t>(ItemEntityId);
								FlecsWorld->each([ContainerId, NewPos, SourceId, &TargetItem](
									flecs::entity E, const FContainedIn& C)
								{
									if (C.ContainerEntityId != ContainerId || !C.IsInGrid() || E.id() == SourceId) return;
									const FItemStaticData* S = E.try_get<FItemStaticData>();
									FIntPoint Size = S ? S->GridSize : FIntPoint(1, 1);
									if (NewPos.X >= C.GridPosition.X && NewPos.X < C.GridPosition.X + Size.X
										&& NewPos.Y >= C.GridPosition.Y && NewPos.Y < C.GridPosition.Y + Size.Y)
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
											FContainerInstance* ContainerInstance = ContainerEntity.try_get_mut<FContainerInstance>();
											if (ContainerInstance) ContainerInstance->CurrentCount--;
											ItemEntity.destruct();
										}
										else
										{
											GridInstance->Occupy(OldPos, ItemSize, ContainerStatic->GridWidth);
										}
										Result = EUIOpResult::Success;
									}
								}
							}

							// Rollback if stack also failed
							if (Result != EUIOpResult::Success)
							{
								GridInstance->Occupy(OldPos, ItemSize, ContainerStatic->GridWidth);
							}
						}
					}
				}
			}

			// Write fresh snapshot to triple buffer (lock-free, ~ns)
			FContainerSnapshot Snap = BuildContainerSnapshot(ContainerId);
			SharedState->SnapshotBuffer.WriteAndSwap(MoveTemp(Snap));

			// Bump version (1 atomic store)
			SharedState->SimVersion.fetch_add(1, std::memory_order_release);

			// Send op result (lock-free MPSC enqueue)
			SharedState->OpResults.Enqueue({OpId, Result});
		});
	};
}

void UFlecsUISubsystem::SetupValueBridge(int64 EntityId)
{
	// Value models use packed atomics written by ECS systems (DeathCheckSystem, WeaponFireSystem).
	// The subsystem reads them on Tick. No additional bridge setup needed.
	// ECS systems will write to the shared state via UFlecsUISubsystem::SelfPtr.
}

// ═══════════════════════════════════════════════════════════════
// SNAPSHOT BUILDER (called on sim thread)
// ═══════════════════════════════════════════════════════════════

FContainerSnapshot UFlecsUISubsystem::BuildContainerSnapshot(int64 ContainerEntityId)
{
	FContainerSnapshot Snap;
	Snap.ContainerEntityId = ContainerEntityId;

	if (!Artillery) return Snap;

	flecs::world* FlecsWorld = Artillery->GetFlecsWorld();
	if (!FlecsWorld) return Snap;

	flecs::entity ContainerEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(ContainerEntityId));
	if (!ContainerEntity.is_alive()) return Snap;

	const FContainerStatic* ContainerStatic = ContainerEntity.try_get<FContainerStatic>();
	const FContainerInstance* ContainerInstance = ContainerEntity.try_get<FContainerInstance>();
	if (!ContainerStatic || !ContainerInstance) return Snap;

	Snap.GridWidth = ContainerStatic->GridWidth;
	Snap.GridHeight = ContainerStatic->GridHeight;
	Snap.MaxWeight = ContainerStatic->MaxWeight;
	Snap.CurrentWeight = ContainerInstance->CurrentWeight;
	Snap.CurrentCount = ContainerInstance->CurrentCount;

	FlecsWorld->each([ContainerEntityId, &Snap](flecs::entity E, const FContainedIn& ContainedIn)
	{
		if (ContainedIn.ContainerEntityId != ContainerEntityId) return;

		const FItemStaticData* StaticData = E.try_get<FItemStaticData>();
		const FItemInstance* ItemInst = E.try_get<FItemInstance>();

		FContainerItemSnapshot ItemSnap;
		ItemSnap.ItemEntityId = static_cast<int64>(E.id());
		// Slot containers: map SlotIndex to grid position (SlotIndex → column, row 0)
		if (ContainedIn.IsInSlot())
			ItemSnap.GridPosition = FIntPoint(ContainedIn.SlotIndex, 0);
		else
			ItemSnap.GridPosition = ContainedIn.GridPosition;
		ItemSnap.GridSize = StaticData ? StaticData->GridSize : FIntPoint(1, 1);
		ItemSnap.TypeId = StaticData ? StaticData->ItemName : NAME_None;
		ItemSnap.Count = ItemInst ? ItemInst->Count : 1;
		ItemSnap.MaxStack = StaticData ? StaticData->MaxStack : 99;
		ItemSnap.Weight = StaticData ? StaticData->Weight : 0.f;

		// ItemDefinition is a UDataAsset — safe to read across threads (immutable, outlives widgets)
		if (StaticData && StaticData->ItemDefinition)
		{
			ItemSnap.ItemDefinition = Cast<UDataAsset>(StaticData->ItemDefinition);
			ItemSnap.RarityTier = StaticData->ItemDefinition->RarityTier;
		}

		Snap.Items.Add(MoveTemp(ItemSnap));
	});

	return Snap;
}
