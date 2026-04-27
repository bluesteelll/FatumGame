// FlecsMultiblockRuntime — SetupStationInstance (Phase 2).
//
// Implementation moved verbatim from FlecsEntitySpawner.cpp's inline crafting
// branch (Phase 1). The only changes vs the original inline block:
//   1. Wrapped in a free function; uses Anchor.world() instead of a spawner
//      member FlecsWorld* to create slot entities.
//   2. checkf precondition: !Anchor.has<FTagCraftingStation>() (v3 PATCH 5 /
//      critique m1). Caller is expected to gate; this traps misuse.
//   3. Log prefix [SetupStationInstance] to disambiguate vs the old
//      [SpawnEntity] log line.
//
// Behaviour is otherwise identical — Phase 1 regression tests must still pass
// after the spawner callsite is updated to call this helper.

#include "Library/FlecsMultiblockRuntime.h"

#include "Async/Async.h"
#include "flecs.h"

#include "FlecsCraftingLog.h"
#include "FlecsCraftingStationProfile.h"
#include "FlecsCraftingTypes.h"
#include "FlecsCraftingUISubsystem.h"
#include "FlecsContainerProfile.h"
#include "FlecsItemComponents.h"
#include "FlecsGameTags.h"
#include "Components/FlecsCraftingComponents.h"
#include "SkeletonTypes.h"

void FlecsMultiblockRuntime::SetupStationInstance(
	flecs::entity Anchor,
	const UFlecsCraftingStationProfile* Profile)
{
	check(Anchor.is_valid() && Anchor.is_alive());
	check(Profile);
	checkf(!Anchor.has<FTagCraftingStation>(),
		TEXT("SetupStationInstance: Anchor entity=%llu already a crafting station — caller must gate."),
		(unsigned long long)Anchor.id());

	flecs::world World = Anchor.world();
	const int64 StationEntityId = static_cast<int64>(Anchor.id());

	FCraftingSlots SlotsComp;
	FFuelSlot FuelSlotComp;
	int32 SlotIdx = 0;

	for (const FSlotLayoutDef& SlotDef : Profile->SlotLayout)
	{
		if (!ensureMsgf(SlotIdx < kMaxCraftingSlots,
			TEXT("SetupStationInstance: station '%s' SlotLayout exceeds kMaxCraftingSlots (%d)"),
			*Profile->GetName(), kMaxCraftingSlots))
		{
			break;
		}
		// Skip slots missing ContainerProfile (authoring-in-progress) — log + continue.
		if (!SlotDef.ContainerProfile)
		{
			UE_LOG(LogCrafting, Warning,
				TEXT("SetupStationInstance: station '%s' slot %d (role=%u) has null ContainerProfile — skipping (SlotLayout index will still advance)"),
				*Profile->GetName(), SlotIdx, static_cast<uint32>(SlotDef.Role));
			++SlotIdx;
			continue;
		}

		// Pure container entity — no physics, no render, no prefab.
		flecs::entity SlotEntity = World.entity();

		FContainerStatic SlotStatic = FContainerStatic::FromProfile(SlotDef.ContainerProfile);
		FContainerInstance SlotInst;
		SlotInst.CurrentWeight = 0.f;
		SlotInst.CurrentCount = 0;
		SlotInst.OwnerEntityId = StationEntityId;

		SlotEntity.set<FContainerStatic>(SlotStatic);
		SlotEntity.set<FContainerInstance>(SlotInst);
		SlotEntity.add<FTagContainer>();

		// Type-specific instance components (mirror the main container branch).
		switch (SlotStatic.Type)
		{
		case EContainerType::Grid:
			{
				FContainerGridInstance GridInst;
				GridInst.Initialize(SlotStatic.GridWidth, SlotStatic.GridHeight);
				SlotEntity.set<FContainerGridInstance>(GridInst);
			}
			break;
		case EContainerType::Slot:
			{
				FContainerSlotsInstance SlotsInst;
				SlotEntity.set<FContainerSlotsInstance>(SlotsInst);
			}
			break;
		case EContainerType::List:
			break;
		}

		// Back-reference so FContainedIn observer can fast-gate to the station.
		FCraftingSlotBackRef BackRef;
		BackRef.StationEntityId = StationEntityId;
		BackRef.SlotIndex = static_cast<uint16>(SlotIdx);
		BackRef.Role = static_cast<uint8>(SlotDef.Role);
		SlotEntity.set<FCraftingSlotBackRef>(BackRef);

		const int64 SlotEntityId = static_cast<int64>(SlotEntity.id());
		SlotsComp.SlotEntityIds[SlotIdx] = SlotEntityId;

		const uint8 RoleIdx = static_cast<uint8>(SlotDef.Role);
		if (RoleIdx < static_cast<uint8>(ESlotRole::MAX))
		{
			++SlotsComp.SlotRoleCounts[RoleIdx];
		}

		if (SlotDef.Role == ESlotRole::Fuel)
		{
			FuelSlotComp.FuelSlotEntityId = SlotEntityId;
		}

		++SlotIdx;
	}

	Anchor.set<FCraftingSlots>(SlotsComp);

	// Fuel slot denormalization — only set when the station actually has a Fuel slot
	// (FromProfile's checkf already enforces <= 1 Fuel slot).
	if (FuelSlotComp.FuelSlotEntityId != 0)
	{
		Anchor.set<FFuelSlot>(FuelSlotComp);
	}

	FCraftingStationInstance StationInst;
	StationInst.bSnapshotDirty = true;  // Initial snapshot published on first flush tick.
	Anchor.set<FCraftingStationInstance>(StationInst);

	// Phase 3 — Smelter-specific instance state. Only attached when the station is a Smelter.
	// (Press / Forge will branch off here in later phases.) Default-constructed = Idle, all zeros.
	if (Profile->StationType == ECraftingStationType::Smelter)
	{
		FSmelterInstance Sm;
		Anchor.set<FSmelterInstance>(Sm);

		UE_LOG(LogCrafting, Log,
			TEXT("[SetupStationInstance] Smelter instance attached to station '%s' entity=%llu"),
			*Profile->StationName.ToString(),
			(unsigned long long)Anchor.id());
	}

	// Register shared state with the UI subsystem (game thread only).
	// Key = FSkeletonKey wrapping the Flecs entity id — matches the lookup key used by
	// CraftingSnapshotFlushSystem when publishing.
	const FSkeletonKey StationKey(static_cast<uint64>(StationEntityId));
	AsyncTask(ENamedThreads::GameThread, [StationKey]()
	{
		if (UFlecsCraftingUISubsystem* UISub = UFlecsCraftingUISubsystem::SelfPtr)
		{
			UISub->CreateSharedState(StationKey);
		}
	});

	UE_LOG(LogCrafting, Log,
		TEXT("[SetupStationInstance] Crafting station set up: profile=%s entity=%llu slots=%d fuelSlot=%lld"),
		*Profile->GetName(), (unsigned long long)Anchor.id(), SlotIdx, FuelSlotComp.FuelSlotEntityId);
}
