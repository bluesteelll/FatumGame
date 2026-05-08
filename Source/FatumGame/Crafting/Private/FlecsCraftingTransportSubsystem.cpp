// UFlecsCraftingTransportSubsystem — Phase 5a impl.
//
// Skeleton + DumpNetworks + console command. RebuildAndPublish body lives here as a
// full BFS over connector segments + station ports per V1 §1.5.1 + V2 PATCH 8.

#include "FlecsCraftingTransportSubsystem.h"

#include "Components/FlecsTransportComponents.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsCraftingLog.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "flecs.h"

#include "Containers/BitArray.h"
#include "Containers/Map.h"

void UFlecsCraftingTransportSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Mirrors UFlecsCraftingUISubsystem dependency declaration so subsystem startup
	// orders correctly relative to UFlecsArtillerySubsystem (which owns the world).
	Collection.InitializeDependency<UFlecsArtillerySubsystem>();
	Super::Initialize(Collection);
	SelfPtr = this;
	UE_LOG(LogCrafting, Log, TEXT("[Transport] UFlecsCraftingTransportSubsystem initialized"));
}

void UFlecsCraftingTransportSubsystem::Deinitialize()
{
	SelfPtr = nullptr;
	SimNetworkRoster.Empty();
	NextNetworkId = 1;
	bRebuildPending.store(false, std::memory_order_relaxed);
	NetworkVersion.store(0, std::memory_order_relaxed);
	UE_LOG(LogCrafting, Log, TEXT("[Transport] UFlecsCraftingTransportSubsystem deinitialized"));
	Super::Deinitialize();
}

bool UFlecsCraftingTransportSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFlecsCraftingTransportSubsystem::QueueRebuild()
{
	bRebuildPending.store(true, std::memory_order_release);
}

bool UFlecsCraftingTransportSubsystem::ConsumeRebuildRequest()
{
	bool bExpected = true;
	return bRebuildPending.compare_exchange_strong(bExpected, false,
		std::memory_order_acq_rel, std::memory_order_relaxed);
}

const FCraftingTransportNetwork* UFlecsCraftingTransportSubsystem::FindNetwork(uint32 NetworkId) const
{
	return SimNetworkRoster.Find(NetworkId);
}

// ─────────────────────────────────────────────────────────────────
// RebuildAndPublish — full BFS rebuild (V1 §1.5.1 + V2 PATCH 8).
//
// Algorithm:
//   1. Slot-mapping pass: assign every placed segment + every station-with-ports
//      a dense slot index 0..N-1. Cap N <= kMaxNodesPerRebuildPass.
//   2. BFS from each unvisited slot. Track visited via TBitArray (1 bit/node).
//      For each visited segment: read FConnectorPlaced front+back targets;
//      enqueue slot for each non-zero target if not visited.
//      For each visited station: walk FStationPorts.Ports[] for connected
//      segments → enqueue each segment slot.
//   3. Per-network kind uniformity check via checkf (designer error).
//   4. Write NetworkId back onto each segment + each port.
// ─────────────────────────────────────────────────────────────────
void UFlecsCraftingTransportSubsystem::RebuildAndPublish(flecs::world& World)
{
	const double StartSeconds = FPlatformTime::Seconds();

	// 1. Reset roster + epoch.
	SimNetworkRoster.Empty();
	NextNetworkId = 1;

	// 2. Slot-mapping pass — collect all segments + all station-ports into a dense int slot map.
	TArray<int64, TInlineAllocator<256>> SegmentIds;
	TArray<int64, TInlineAllocator<256>> StationIds;
	TMap<int64, int32> SlotByEntityId;   // dense int32 slot per entity-id
	int32 NextSlot = 0;

	flecs::query<const FConnectorPlaced> SegmentQuery = World
		.query_builder<const FConnectorPlaced>()
		.with<FTagConnectorPlaced>()
		.build();
	SegmentQuery.each([&](flecs::entity E, const FConnectorPlaced& /*P*/)
	{
		if (NextSlot >= static_cast<int32>(kMaxNodesPerRebuildPass)) return;
		const int64 Id = static_cast<int64>(E.id());
		SlotByEntityId.Add(Id, NextSlot);
		SegmentIds.Add(Id);
		++NextSlot;
	});

	flecs::query<const FStationPorts> StationQuery = World
		.query_builder<const FStationPorts>()
		.build();
	StationQuery.each([&](flecs::entity E, const FStationPorts& /*Ports*/)
	{
		if (NextSlot >= static_cast<int32>(kMaxNodesPerRebuildPass)) return;
		const int64 Id = static_cast<int64>(E.id());
		SlotByEntityId.Add(Id, NextSlot);
		StationIds.Add(Id);
		++NextSlot;
	});

	checkf(NextSlot <= static_cast<int32>(kMaxNodesPerRebuildPass),
		TEXT("[Transport] RebuildAndPublish: NextSlot=%d exceeds kMaxNodesPerRebuildPass=%u"),
		NextSlot, kMaxNodesPerRebuildPass);

	const int32 SegmentSlotCount = SegmentIds.Num();
	// StationIds are in slots [SegmentSlotCount, NextSlot). Use this fact for is-segment check.
	auto IsSegmentSlot = [SegmentSlotCount](int32 Slot) { return Slot < SegmentSlotCount; };

	if (NextSlot == 0)
	{
		// No nodes — nothing to rebuild.
		NetworkVersion.fetch_add(1, std::memory_order_release);
		const double ElapsedUs = (FPlatformTime::Seconds() - StartSeconds) * 1e6;
		UE_LOG(LogCrafting, Log,
			TEXT("[Transport] Rebuild: networks=0 (Liquid=0, Power=0) segments=0 stations=0 in %.2f us"),
			ElapsedUs);
		return;
	}

	// 3. BFS loop.
	TBitArray<TInlineAllocator<8>> Visited(false, NextSlot);
	TArray<int32, TInlineAllocator<128>> Queue;

	int32 LiquidCount = 0;
	int32 PowerCount  = 0;
	int32 TotalSegments = 0;

	// M3 — explicit dedup helper used for BOTH segment-targets and station-targets.
	auto TryEnqueue = [&](int64 TargetId)
	{
		if (TargetId == 0) return;
		const int32* SlotPtr = SlotByEntityId.Find(TargetId);
		if (!SlotPtr) return;             // not a tracked node (e.g. dead, or peer ref to non-segment non-station)
		if (Visited[*SlotPtr]) return;    // already visited
		Visited[*SlotPtr] = true;
		Queue.Push(*SlotPtr);
	};

	for (int32 RootSlot = 0; RootSlot < NextSlot; ++RootSlot)
	{
		if (Visited[RootSlot]) continue;

		// Determine network Kind from the FIRST node we visit.
		ETransportKind RootKind = ETransportKind::None;
		if (IsSegmentSlot(RootSlot))
		{
			const int64 RootId = SegmentIds[RootSlot];
			flecs::entity RootE = World.entity(static_cast<flecs::entity_t>(RootId));
			if (RootE.is_alive())
			{
				if (const FConnectorSegmentStatic* Seg = RootE.try_get<FConnectorSegmentStatic>())
				{
					if (Seg->Role == EConnectorTransportRole::Liquid) RootKind = ETransportKind::Liquid;
					else if (Seg->Role == EConnectorTransportRole::Power) RootKind = ETransportKind::Power;
				}
			}
		}
		else
		{
			const int32 StationLocalIdx = RootSlot - SegmentSlotCount;
			const int64 RootId = StationIds[StationLocalIdx];
			flecs::entity RootE = World.entity(static_cast<flecs::entity_t>(RootId));
			if (RootE.is_alive())
			{
				if (const FStationPorts* Ports = RootE.try_get<FStationPorts>())
				{
					for (const FPortSlot& P : Ports->Ports)
					{
						if (P.Kind == EPortKind::LiquidOutlet || P.Kind == EPortKind::LiquidInlet)
						{
							RootKind = ETransportKind::Liquid;
							break;
						}
						if (P.Kind == EPortKind::PowerOutlet || P.Kind == EPortKind::PowerInlet)
						{
							RootKind = ETransportKind::Power;
							break;
						}
					}
				}
			}
		}

		if (RootKind == ETransportKind::None)
		{
			// Orphan node — segment with Role None (defective) OR station with no transport ports.
			// Mark visited but don't allocate a network row.
			Visited[RootSlot] = true;
			continue;
		}

		const uint32 NetId = NextNetworkId++;
		checkf(NetId < kMaxNetworksPerScene,
			TEXT("[Transport] RebuildAndPublish: NetworkId %u exceeds kMaxNetworksPerScene=%u"),
			NetId, kMaxNetworksPerScene);

		FCraftingTransportNetwork Network;
		Network.NetworkId = NetId;
		Network.Kind      = RootKind;

		Visited[RootSlot] = true;
		Queue.Reset();
		Queue.Push(RootSlot);

		while (Queue.Num() > 0)
		{
			const int32 Slot = Queue.Pop(EAllowShrinking::No);

			if (IsSegmentSlot(Slot))
			{
				const int64 SegId = SegmentIds[Slot];
				flecs::entity SegE = World.entity(static_cast<flecs::entity_t>(SegId));
				if (!SegE.is_alive()) continue;

				FConnectorPlaced* Placed = SegE.try_get_mut<FConnectorPlaced>();
				if (!Placed) continue;

				// Kind uniformity invariant.
				if (const FConnectorSegmentStatic* Stat = SegE.try_get<FConnectorSegmentStatic>())
				{
					const ETransportKind SegKind = (Stat->Role == EConnectorTransportRole::Liquid)
						? ETransportKind::Liquid
						: (Stat->Role == EConnectorTransportRole::Power)
							? ETransportKind::Power
							: ETransportKind::None;
					checkf(SegKind == RootKind,
						TEXT("[Transport] BFS kind mismatch: segment=%llu role=%u expected=%u"),
						(unsigned long long)SegId, (uint32)Stat->Role, (uint32)RootKind);
				}

				// Append + enforce per-network cap.
				Network.SegmentIds.Add(SegId);
				checkf(Network.SegmentIds.Num() <= static_cast<int32>(kMaxSegmentsPerNetwork),
					TEXT("[Transport] BFS segment overflow: net=%u count=%d cap=%u"),
					NetId, Network.SegmentIds.Num(), kMaxSegmentsPerNetwork);

				Placed->NetworkId = NetId;
				++TotalSegments;

				TryEnqueue(Placed->FrontSnapTargetId);
				TryEnqueue(Placed->BackSnapTargetId);
			}
			else
			{
				const int32 StationLocalIdx = Slot - SegmentSlotCount;
				const int64 StationId = StationIds[StationLocalIdx];
				flecs::entity StE = World.entity(static_cast<flecs::entity_t>(StationId));
				if (!StE.is_alive()) continue;

				FStationPorts* Ports = StE.try_get_mut<FStationPorts>();
				if (!Ports) continue;

				bool bAnyOutlet = false;
				bool bAnyInlet  = false;
				for (FPortSlot& P : Ports->Ports)
				{
					// Kind uniformity per-port.
					const bool bIsLiquid = (P.Kind == EPortKind::LiquidOutlet || P.Kind == EPortKind::LiquidInlet);
					const bool bIsPower  = (P.Kind == EPortKind::PowerOutlet  || P.Kind == EPortKind::PowerInlet);
					if (P.ConnectedSegmentId != 0)
					{
						const ETransportKind PortKind = bIsLiquid
							? ETransportKind::Liquid
							: bIsPower
								? ETransportKind::Power
								: ETransportKind::None;
						checkf(PortKind == RootKind,
							TEXT("[Transport] BFS station-port kind mismatch: station=%llu port-kind=%u expected=%u"),
							(unsigned long long)StationId, (uint32)P.Kind, (uint32)RootKind);

						P.NetworkId = static_cast<uint8>(NetId);

						if (P.Kind == EPortKind::LiquidOutlet || P.Kind == EPortKind::PowerOutlet) bAnyOutlet = true;
						if (P.Kind == EPortKind::LiquidInlet  || P.Kind == EPortKind::PowerInlet)  bAnyInlet  = true;

						TryEnqueue(P.ConnectedSegmentId);
					}
					else
					{
						// Vacant ports clear NetworkId.
						P.NetworkId = 0;
					}
				}

				Network.StationIds.AddUnique(StationId);
				if (bAnyOutlet) Network.OutletStationIds.AddUnique(StationId);
				if (bAnyInlet)  Network.InletStationIds.AddUnique(StationId);
			}
		}

		if (Network.Kind == ETransportKind::Liquid) ++LiquidCount;
		else if (Network.Kind == ETransportKind::Power) ++PowerCount;

		SimNetworkRoster.Add(NetId, MoveTemp(Network));
	}

	NetworkVersion.fetch_add(1, std::memory_order_release);

	const double ElapsedUs = (FPlatformTime::Seconds() - StartSeconds) * 1e6;
	UE_LOG(LogCrafting, Log,
		TEXT("[Transport] Rebuild: networks=%d (Liquid=%d, Power=%d) segments=%d stations=%d in %.2f us"),
		SimNetworkRoster.Num(), LiquidCount, PowerCount, TotalSegments, StationIds.Num(), ElapsedUs);
}

// ─────────────────────────────────────────────────────────────────
// DumpNetworks — game-thread debug console hook.
// ─────────────────────────────────────────────────────────────────
void UFlecsCraftingTransportSubsystem::DumpNetworks() const
{
	const uint32 Ver = NetworkVersion.load(std::memory_order_acquire);
	UE_LOG(LogCrafting, Display,
		TEXT("[Transport] DumpNetworks (NOT thread-safe — torn read possible if sim is mid-rebuild) ver=%u count=%d"),
		Ver, SimNetworkRoster.Num());

	for (const auto& Pair : SimNetworkRoster)
	{
		const FCraftingTransportNetwork& Net = Pair.Value;
		UE_LOG(LogCrafting, Display,
			TEXT("  Network id=%u kind=%s segments=%d stations=%d (outlets=%d inlets=%d)"),
			Net.NetworkId,
			Net.Kind == ETransportKind::Liquid ? TEXT("Liquid")
				: Net.Kind == ETransportKind::Power ? TEXT("Power") : TEXT("None"),
			Net.SegmentIds.Num(),
			Net.StationIds.Num(),
			Net.OutletStationIds.Num(),
			Net.InletStationIds.Num());
	}
}

// ─────────────────────────────────────────────────────────────────
// Console command — fatum.Crafting.DumpNetworks
// ─────────────────────────────────────────────────────────────────
static FAutoConsoleCommandWithWorld GDumpNetworksCmd(
	TEXT("fatum.Crafting.DumpNetworks"),
	TEXT("Dump live transport networks (kind / segment count / outlet+inlet station count)"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (!World) return;
		if (UFlecsCraftingTransportSubsystem* Sub = World->GetSubsystem<UFlecsCraftingTransportSubsystem>())
		{
			Sub->DumpNetworks();
		}
	}));
