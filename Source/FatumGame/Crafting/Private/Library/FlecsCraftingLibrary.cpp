// UFlecsCraftingLibrary — Blueprint-facing crafting API.

#include "FlecsCraftingLibrary.h"
#include "FlecsCraftingRuntime.h"
#include "Components/FlecsCraftingComponents.h"
#include "Components/FlecsMultiblockComponents.h"
#include "Components/FlecsTransportComponents.h"     // Phase 5a — connector components
#include "FlecsCraftingLog.h"
#include "FlecsCraftingTransportSubsystem.h"          // Phase 5a — QueueRebuild
#include "FlecsCraftingUISubsystem.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsLibraryHelpers.h"
#include "FlecsItemComponents.h"
#include "FlecsItemDefinition.h"
#include "FlecsEntityComponents.h"
#include "FlecsEntityDefinition.h"
#include "FlecsMultiblockBlueprint.h"
#include "FlecsEntitySpawner.h"                       // Phase 5a — FEntitySpawnRequest, SpawnEntity
#include "FlecsBarrageComponents.h"                   // Phase 5a — FBarrageBody (freeze body to Static)
#include "BarrageDispatch.h"                          // Phase 5a — SetBodyMotionType, CastRay (re-validate)
#include "FBarragePrimitive.h"
#include "EPhysicsLayer.h"
#include "IsolatedJoltIncludes.h"                     // JPH::EMotionType + body filter
#include "PhysicsFilters/FastObjectLayerFilters.h"
#include "Library/FlecsMultiblockRuntime.h"
#include "flecs.h"
#include "Async/Async.h"

namespace
{
	/** Phase 4 — resolve an FSkeletonKey wrapping a Flecs entity id to the entity.
	 *  Used by RequestPartDetach/Attach/Deconstruct entry points where the key is
	 *  the entity id directly (NOT a Barrage key). Mirrors PerformInteractionTrace's
	 *  CraftingHoverTarget convention (post commit 64f54e3). */
	flecs::entity GetEntityFromFlecsIdKey(UFlecsArtillerySubsystem* Sub, FSkeletonKey Key)
	{
		if (!Sub || !Key.IsValid()) return flecs::entity();
		flecs::world* W = Sub->GetFlecsWorld();
		if (!W) return flecs::entity();
		const uint64 Id = static_cast<uint64>(Key.Obj);
		flecs::entity E = W->entity(static_cast<flecs::entity_t>(Id));
		return E.is_valid() ? E : flecs::entity();
	}
}

// ═══════════════════════════════════════════════════════════════
// COMMAND API
// ═══════════════════════════════════════════════════════════════

void UFlecsCraftingLibrary::RequestMatch(UObject* WorldContextObject, FSkeletonKey StationKey)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !StationKey.IsValid()) return;

	Subsystem->EnqueueCommand([Subsystem, StationKey]()
	{
		flecs::entity StationE = FlecsLibrary::GetEntityForKey(Subsystem, StationKey);
		if (!StationE.is_valid() || !StationE.is_alive()) return;
		if (!StationE.has<FTagCraftingStation>()) return;

		FlecsCraftingRuntime::InvalidateMatchCache(StationE);
	});
}

void UFlecsCraftingLibrary::RequestStationDestroy(UObject* WorldContextObject, FSkeletonKey StationKey)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !StationKey.IsValid()) return;

	Subsystem->EnqueueCommand([Subsystem, StationKey]()
	{
		flecs::world* World = Subsystem->GetFlecsWorld();
		if (!World) return;

		flecs::entity StationE = FlecsLibrary::GetEntityForKey(Subsystem, StationKey);
		if (!StationE.is_valid() || !StationE.is_alive()) return;
		if (!StationE.has<FTagCraftingStation>()) return;

		// Step 1 — seal the station so observers / library hooks early-exit.
		StationE.add<FTagCraftingStationDestroying>();

		// Step 2 — drain every slot container, then destruct the slot entity.
		if (const FCraftingSlots* Slots = StationE.try_get<FCraftingSlots>())
		{
			for (int32 i = 0; i < kMaxCraftingSlots; ++i)
			{
				const int64 SlotId = Slots->SlotEntityIds[i];
				if (SlotId == 0) continue;

				flecs::entity SlotE = World->entity(static_cast<flecs::entity_t>(SlotId));
				if (!SlotE.is_alive()) continue;

				// Enumerate-then-destruct so we do not mutate the iterator.
				TArray<flecs::entity> ItemsToDestroy;
				World->each([SlotId, &ItemsToDestroy](flecs::entity ItemE, const FContainedIn& CI)
				{
					if (CI.ContainerEntityId == SlotId)
					{
						ItemsToDestroy.Add(ItemE);
					}
				});
				for (flecs::entity ItemE : ItemsToDestroy)
				{
					if (ItemE.is_alive()) ItemE.destruct();
				}

				SlotE.destruct();
			}
		}

		// Step 3 — destruct the station itself.
		StationE.destruct();

		// Step 4 — release the snapshot shared state on game thread.
		// Symmetric to FlecsEntitySpawner::SpawnEntity Step 13c's CreateSharedState AsyncTask.
		AsyncTask(ENamedThreads::GameThread, [StationKey]()
		{
			if (UFlecsCraftingUISubsystem* UISub = UFlecsCraftingUISubsystem::SelfPtr)
			{
				UISub->DestroySharedState(StationKey);
			}
		});

		UE_LOG(LogCrafting, Log, TEXT("Station destroyed (key=0x%llX)"),
			static_cast<unsigned long long>(StationKey.Obj));
	});
}

// ═══════════════════════════════════════════════════════════════
// SMELTER (Phase 3)
// ═══════════════════════════════════════════════════════════════

void UFlecsCraftingLibrary::RequestSmelterStart(UObject* WorldContextObject, FSkeletonKey StationKey)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !StationKey.IsValid())
	{
		UE_LOG(LogCrafting, Verbose, TEXT("RequestSmelterStart: invalid station key"));
		return;
	}

	Subsystem->EnqueueCommand([Subsystem, StationKey]()
	{
		flecs::entity StationE = FlecsLibrary::GetEntityForKey(Subsystem, StationKey);
		if (!StationE.is_valid() || !StationE.is_alive())
		{
			UE_LOG(LogCrafting, Verbose,
				TEXT("RequestSmelterStart: station entity not alive (key=0x%llX)"),
				(unsigned long long)StationKey.Obj);
			return;
		}
		if (!StationE.has<FTagCraftingStation>())
		{
			UE_LOG(LogCrafting, Warning,
				TEXT("RequestSmelterStart: entity %llu is not a crafting station"),
				(unsigned long long)StationE.id());
			return;
		}

		FSmelterInstance* SmInst = StationE.try_get_mut<FSmelterInstance>();
		if (!SmInst)
		{
			UE_LOG(LogCrafting, Warning,
				TEXT("RequestSmelterStart: entity %llu has no FSmelterInstance — not a smelter"),
				(unsigned long long)StationE.id());
			return;
		}

		SmInst->bStartRequested = true;
		UE_LOG(LogCrafting, Log,
			TEXT("RequestSmelterStart: queued for station entity=%llu (Phase=%u)"),
			(unsigned long long)StationE.id(),
			static_cast<uint32>(SmInst->Phase));
	});
}

void UFlecsCraftingLibrary::RequestSmelterCancel(UObject* WorldContextObject, FSkeletonKey StationKey)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !StationKey.IsValid())
	{
		UE_LOG(LogCrafting, Verbose, TEXT("RequestSmelterCancel: invalid station key"));
		return;
	}

	Subsystem->EnqueueCommand([Subsystem, StationKey]()
	{
		flecs::entity StationE = FlecsLibrary::GetEntityForKey(Subsystem, StationKey);
		if (!StationE.is_valid() || !StationE.is_alive())
		{
			UE_LOG(LogCrafting, Verbose,
				TEXT("RequestSmelterCancel: station entity not alive (key=0x%llX)"),
				(unsigned long long)StationKey.Obj);
			return;
		}
		if (!StationE.has<FTagCraftingStation>())
		{
			UE_LOG(LogCrafting, Warning,
				TEXT("RequestSmelterCancel: entity %llu is not a crafting station"),
				(unsigned long long)StationE.id());
			return;
		}

		FSmelterInstance* SmInst = StationE.try_get_mut<FSmelterInstance>();
		if (!SmInst)
		{
			UE_LOG(LogCrafting, Warning,
				TEXT("RequestSmelterCancel: entity %llu has no FSmelterInstance — not a smelter"),
				(unsigned long long)StationE.id());
			return;
		}

		SmInst->bCancelRequested = true;
		UE_LOG(LogCrafting, Log,
			TEXT("RequestSmelterCancel: queued for station entity=%llu (Phase=%u)"),
			(unsigned long long)StationE.id(),
			static_cast<uint32>(SmInst->Phase));
	});
}

// ═══════════════════════════════════════════════════════════════
// PHASE 4 — MODULAR STATIONS (wrench attach/detach/deconstruct)
// ═══════════════════════════════════════════════════════════════

void UFlecsCraftingLibrary::RequestPartDetach(UObject* WorldContextObject, FSkeletonKey ChildKey)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !ChildKey.IsValid())
	{
		UE_LOG(LogCrafting, Verbose, TEXT("RequestPartDetach: invalid input"));
		return;
	}

	Subsystem->EnqueueCommand([Subsystem, ChildKey]()
	{
		flecs::entity ChildE = GetEntityFromFlecsIdKey(Subsystem, ChildKey);
		if (!ChildE.is_alive())
		{
			UE_LOG(LogCrafting, Verbose,
				TEXT("RequestPartDetach: child entity not alive (key=0x%llX)"),
				(unsigned long long)ChildKey.Obj);
			return;
		}
		FlecsMultiblockRuntime::DetachPartFromStation(ChildE);
	});
}

void UFlecsCraftingLibrary::RequestPartAttach(UObject* WorldContextObject,
	FSkeletonKey StationKey, int32 PortIndex, int64 PartItemEntityId)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !StationKey.IsValid() || PartItemEntityId == 0)
	{
		UE_LOG(LogCrafting, Verbose, TEXT("RequestPartAttach: invalid input"));
		return;
	}

	// CRITICAL #1 — capture UWorld on the GAME THREAD here. AttachPartToStation runs
	// inside the EnqueueCommand lambda (sim thread) and MUST NOT touch UObjectArray-backed
	// APIs like Subsystem->GetWorld(). The TWeakObjectPtr is forwarded into the lambda;
	// AsyncTask(GameThread) inside AttachPartToStation calls .Get() back on game thread.
	UWorld* WorldCtx = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!WorldCtx)
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("RequestPartAttach: no UWorld from WorldContextObject"));
		return;
	}
	TWeakObjectPtr<UWorld> WeakWorld = WorldCtx;

	Subsystem->EnqueueCommand([Subsystem, StationKey, PortIndex, PartItemEntityId, WeakWorld]()
	{
		flecs::world* W = Subsystem->GetFlecsWorld();
		if (!W) return;
		flecs::entity StationE = GetEntityFromFlecsIdKey(Subsystem, StationKey);
		flecs::entity PartItem = W->entity(static_cast<flecs::entity_t>(PartItemEntityId));
		if (!StationE.is_alive())
		{
			UE_LOG(LogCrafting, Warning,
				TEXT("RequestPartAttach: station entity not alive (key=0x%llX)"),
				(unsigned long long)StationKey.Obj);
			return;
		}
		if (!PartItem.is_alive())
		{
			UE_LOG(LogCrafting, Warning,
				TEXT("RequestPartAttach: part item entity not alive (id=%lld)"),
				PartItemEntityId);
			return;
		}
		FlecsMultiblockRuntime::AttachPartToStation(PartItem, StationE, PortIndex, WeakWorld);
	});
}

void UFlecsCraftingLibrary::RequestPartAttachAuto(UObject* WorldContextObject,
	FSkeletonKey StationKey, int32 PortIndex, int64 PlayerInventoryEntityId)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !StationKey.IsValid() || PlayerInventoryEntityId == 0)
	{
		UE_LOG(LogCrafting, Verbose, TEXT("RequestPartAttachAuto: invalid input"));
		return;
	}

	// CRITICAL #1 — capture UWorld on game thread (see RequestPartAttach for rationale).
	UWorld* WorldCtx = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!WorldCtx)
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("RequestPartAttachAuto: no UWorld from WorldContextObject"));
		return;
	}
	TWeakObjectPtr<UWorld> WeakWorld = WorldCtx;

	Subsystem->EnqueueCommand([Subsystem, StationKey, PortIndex, PlayerInventoryEntityId, WeakWorld]()
	{
		flecs::world* W = Subsystem->GetFlecsWorld();
		if (!W) return;
		flecs::entity StationE = GetEntityFromFlecsIdKey(Subsystem, StationKey);
		if (!StationE.is_alive())
		{
			UE_LOG(LogCrafting, Verbose,
				TEXT("RequestPartAttachAuto: station entity not alive (key=0x%llX)"),
				(unsigned long long)StationKey.Obj);
			return;
		}

		// Resolve port acceptance set.
		const UFlecsMultiblockBlueprint* BP = FlecsMultiblockRuntime::ResolveBlueprintForStation(StationE);
		if (!BP || !BP->ExtensionPorts.IsValidIndex(PortIndex))
		{
			UE_LOG(LogCrafting, Warning,
				TEXT("RequestPartAttachAuto: invalid port index %d"), PortIndex);
			return;
		}
		const TArray<FName>& Accepted = BP->ExtensionPorts[PortIndex].AcceptedPartRoles;

		// Walk player inventory: find first item whose EntityDefinition->MultiblockPartRole
		// is in the port's accepted-roles set. Containers can have nested items via
		// FContainedIn; we walk by container id (player inventory is one container entity).
		const int64 InventoryId = PlayerInventoryEntityId;
		flecs::entity Picked;
		W->each([InventoryId, &Accepted, &Picked](flecs::entity ItemE, const FContainedIn& CI)
		{
			if (Picked.is_valid()) return;  // already found
			if (CI.ContainerEntityId != InventoryId) return;
			const FEntityDefinitionRef* DefRef = ItemE.try_get<FEntityDefinitionRef>();
			if (!DefRef || !DefRef->Definition) return;
			const FName Role = DefRef->Definition->MultiblockPartRole;
			if (!Accepted.IsEmpty() && !Accepted.Contains(Role)) return;
			Picked = ItemE;
		});

		if (!Picked.is_valid() || !Picked.is_alive())
		{
			UE_LOG(LogCrafting, Verbose,
				TEXT("RequestPartAttachAuto: no compatible part for port %d in inventory %lld"),
				PortIndex, InventoryId);
			return;
		}

		FlecsMultiblockRuntime::AttachPartToStation(Picked, StationE, PortIndex, WeakWorld);
	});
}

void UFlecsCraftingLibrary::RequestStationDeconstruct(UObject* WorldContextObject, FSkeletonKey StationKey)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !StationKey.IsValid())
	{
		UE_LOG(LogCrafting, Verbose, TEXT("RequestStationDeconstruct: invalid input"));
		return;
	}

	Subsystem->EnqueueCommand([Subsystem, StationKey]()
	{
		flecs::entity StationE = GetEntityFromFlecsIdKey(Subsystem, StationKey);
		if (!StationE.is_alive())
		{
			UE_LOG(LogCrafting, Verbose,
				TEXT("RequestStationDeconstruct: station entity not alive (key=0x%llX)"),
				(unsigned long long)StationKey.Obj);
			return;
		}
		FlecsMultiblockRuntime::DeconstructStation(StationE);
	});
}

bool UFlecsCraftingLibrary::IsPartSwappable(UObject* WorldContextObject, FSkeletonKey ChildKey)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !ChildKey.IsValid()) return false;

	flecs::entity ChildE = GetEntityFromFlecsIdKey(Subsystem, ChildKey);
	if (!ChildE.is_alive()) return false;

	const FMultiblockChildOf* Back = ChildE.try_get<FMultiblockChildOf>();
	if (!Back) return false;

	flecs::world* W = Subsystem->GetFlecsWorld();
	if (!W) return false;

	flecs::entity Anchor = W->entity(static_cast<flecs::entity_t>(Back->AnchorEntityId));
	if (!Anchor.is_alive()) return false;

	const FMultiblockChildren* Roster = Anchor.try_get<FMultiblockChildren>();
	if (!Roster) return false;

	for (int32 i = 0; i < Roster->ChildCount; ++i)
	{
		if (Roster->ChildSlots[i].ChildEntityId == static_cast<int64>(ChildE.id()))
		{
			return Roster->ChildSlots[i].bSwappable != 0;
		}
	}
	return false;
}

// ═══════════════════════════════════════════════════════════════
// UI HELPERS
// ═══════════════════════════════════════════════════════════════

FText UFlecsCraftingLibrary::GetDiagnosticText(ECraftingMatchDiagnostic Diagnostic)
{
	switch (Diagnostic)
	{
	case ECraftingMatchDiagnostic::None:                return FText::GetEmpty();
	case ECraftingMatchDiagnostic::NoMatch:             return NSLOCTEXT("Crafting", "Diag_NoMatch", "No Match");
	case ECraftingMatchDiagnostic::PartialIngredients:  return NSLOCTEXT("Crafting", "Diag_PartialIngredients", "Partial Ingredients");
	case ECraftingMatchDiagnostic::WrongStation:        return NSLOCTEXT("Crafting", "Diag_WrongStation", "Wrong Station");
	case ECraftingMatchDiagnostic::WrongFuel:           return NSLOCTEXT("Crafting", "Diag_WrongFuel", "Wrong Fuel");
	case ECraftingMatchDiagnostic::InsufficientFuel:    return NSLOCTEXT("Crafting", "Diag_InsufficientFuel", "Insufficient Fuel");
	case ECraftingMatchDiagnostic::MultipleMatches:     return NSLOCTEXT("Crafting", "Diag_MultipleMatches", "Multiple Matches");
	default:                                            return FText::GetEmpty();
	}
}

FText UFlecsCraftingLibrary::GetSlotRoleText(ESlotRole Role)
{
	switch (Role)
	{
	case ESlotRole::MaterialInput: return NSLOCTEXT("Crafting", "Role_MaterialInput", "Material Input");
	case ESlotRole::Fuel:          return NSLOCTEXT("Crafting", "Role_Fuel", "Fuel");
	case ESlotRole::Output:        return NSLOCTEXT("Crafting", "Role_Output", "Output");
	case ESlotRole::Die:           return NSLOCTEXT("Crafting", "Role_Die", "Die");
	case ESlotRole::Tool:          return NSLOCTEXT("Crafting", "Role_Tool", "Tool");
	case ESlotRole::Internal:      return NSLOCTEXT("Crafting", "Role_Internal", "Internal");
	default:                       return FText::GetEmpty();
	}
}

// ═══════════════════════════════════════════════════════════════
// PHASE 5a — CONNECTOR PLACEMENT / DETACH (V2 PATCH 1)
// ═══════════════════════════════════════════════════════════════

void UFlecsCraftingLibrary::RequestConnectorPlace(
	UObject* WorldContextObject,
	UFlecsEntityDefinition* ConnectorDef,
	FVector FrontWorld, FVector BackWorld,
	int64 FrontSnapTargetId, int32 FrontPortIndex,
	int64 BackSnapTargetId,  int32 BackPortIndex)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !ConnectorDef ||
		ConnectorDef->ConnectorTransportRole == EConnectorTransportRole::None)
	{
		UE_LOG(LogCrafting, Verbose, TEXT("[Transport] RequestConnectorPlace: invalid input"));
		return;
	}
	// 5a: dangling endpoints rejected (both ends MUST snap to a station port).
	if (FrontSnapTargetId == 0 || BackSnapTargetId == 0 ||
		FrontPortIndex < 0 || BackPortIndex < 0)
	{
		UE_LOG(LogCrafting, Verbose,
			TEXT("[Transport] RequestConnectorPlace: dangling endpoint rejected (front=%lld:%d back=%lld:%d)"),
			FrontSnapTargetId, FrontPortIndex, BackSnapTargetId, BackPortIndex);
		return;
	}

	// PATCH M4: capture UWorld + subsystem as weak refs on game thread BEFORE EnqueueCommand,
	// so AsyncTask round-trip survives PIE shutdown gracefully (5-tick timeout backstop).
	UWorld* WorldCtx = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!WorldCtx)
	{
		UE_LOG(LogCrafting, Warning, TEXT("[Transport] RequestConnectorPlace: no UWorld"));
		return;
	}
	TWeakObjectPtr<UWorld> WeakWorld = WorldCtx;
	TWeakObjectPtr<UFlecsArtillerySubsystem> WeakArt = Subsystem;
	const EConnectorTransportRole Role = ConnectorDef->ConnectorTransportRole;
	UFlecsEntityDefinition* CapturedDef = ConnectorDef;

	Subsystem->EnqueueCommand([Subsystem, CapturedDef, FrontWorld, BackWorld,
		FrontSnapTargetId, FrontPortIndex, BackSnapTargetId, BackPortIndex,
		Role, WeakWorld, WeakArt]()
	{
		// Step 1 — RE-VALIDATE.
		flecs::world* WorldPtr = Subsystem->GetFlecsWorld();
		if (!WorldPtr) return;
		flecs::world W = *WorldPtr;

		flecs::entity FrontT = W.entity(static_cast<flecs::entity_t>(FrontSnapTargetId));
		flecs::entity BackT  = W.entity(static_cast<flecs::entity_t>(BackSnapTargetId));
		if (!FrontT.is_alive() || !BackT.is_alive() ||
			FrontT.has<FTagCraftingStationDestroying>() ||
			BackT.has<FTagCraftingStationDestroying>())
		{
			UE_LOG(LogCrafting, Verbose, TEXT("[Transport] place rejected: dead target"));
			return;
		}
		// Same-port self-snap defensive check.
		if (FrontSnapTargetId == BackSnapTargetId && FrontPortIndex == BackPortIndex)
		{
			UE_LOG(LogCrafting, Verbose, TEXT("[Transport] place rejected: same port self-snap"));
			return;
		}

		auto ValidateStationSide = [Role](flecs::entity Target, int32 PortIdx) -> bool
		{
			if (Target.has<FPendingConnectorPlace>())
			{
				UE_LOG(LogCrafting, Verbose,
					TEXT("[Transport] place rejected: station=%llu has pending connector place"),
					(unsigned long long)Target.id());
				return false;
			}
			// PATCH M1: Smelter Phase guard via try_get; absence = not-busy.
			if (const FSmelterInstance* Sm = Target.try_get<FSmelterInstance>())
			{
				if (Sm->Phase != EProcessPhase::Idle && Sm->Phase != EProcessPhase::Disabled)
				{
					UE_LOG(LogCrafting, Warning,
						TEXT("[Transport] place rejected: Smelter station=%llu mid-process Phase=%u"),
						(unsigned long long)Target.id(), (uint32)Sm->Phase);
					return false;
				}
			}
			const FStationPorts* Ports = Target.try_get<FStationPorts>();
			if (!Ports || !Ports->Ports.IsValidIndex(PortIdx))
			{
				UE_LOG(LogCrafting, Warning,
					TEXT("[Transport] place rejected: station=%llu has no port index %d"),
					(unsigned long long)Target.id(), PortIdx);
				return false;
			}
			const FPortSlot& Slot = Ports->Ports[PortIdx];
			if (Slot.ConnectedSegmentId != 0)
			{
				UE_LOG(LogCrafting, Verbose,
					TEXT("[Transport] place rejected: station=%llu port=%d already occupied (segment=%lld)"),
					(unsigned long long)Target.id(), PortIdx, Slot.ConnectedSegmentId);
				return false;
			}
			// Kind / Role match.
			const bool bRoleOk =
				(Role == EConnectorTransportRole::Liquid &&
					(Slot.Kind == EPortKind::LiquidOutlet || Slot.Kind == EPortKind::LiquidInlet)) ||
				(Role == EConnectorTransportRole::Power &&
					(Slot.Kind == EPortKind::PowerOutlet  || Slot.Kind == EPortKind::PowerInlet));
			if (!bRoleOk)
			{
				UE_LOG(LogCrafting, Warning,
					TEXT("[Transport] place rejected: kind mismatch station=%llu port=%d kind=%u role=%u"),
					(unsigned long long)Target.id(), PortIdx,
					(uint32)Slot.Kind, (uint32)Role);
				return false;
			}
			return true;
		};
		if (!ValidateStationSide(FrontT, FrontPortIndex)) return;
		if (!ValidateStationSide(BackT,  BackPortIndex))  return;

		// Step 2 — RESERVE atomically.
		const uint64 NowTick = WorldPtr->get_info()->frame_count_total;
		FPendingConnectorPlace Pf; Pf.EnqueuedTickStamp = NowTick; Pf.ReservedPortIndex = (uint8)FrontPortIndex;
		FPendingConnectorPlace Pb; Pb.EnqueuedTickStamp = NowTick; Pb.ReservedPortIndex = (uint8)BackPortIndex;
		FrontT.set<FPendingConnectorPlace>(Pf);
		BackT.set<FPendingConnectorPlace>(Pb);

		// Step 3 — AsyncTask GameThread → SpawnEntity.
		AsyncTask(ENamedThreads::GameThread, [WeakWorld, WeakArt, CapturedDef,
			FrontWorld, BackWorld, FrontSnapTargetId, BackSnapTargetId,
			FrontPortIndex, BackPortIndex]()
		{
			UWorld* W = WeakWorld.Get();
			UFlecsArtillerySubsystem* Art = WeakArt.Get();
			if (!W || !Art)
			{
				// PIE exit between dispatch and game-thread continuation.
				// The 60-tick ClearStaleConnectorReservationsSystem is the timeout backstop.
				UE_LOG(LogCrafting, Warning,
					TEXT("[Transport] AsyncTask spawn skipped: WeakWorld/WeakArt stale; reservations will time out"));
				return;
			}
			const FVector MidPoint = (FrontWorld + BackWorld) * 0.5;
			const FRotator Rot = (BackWorld - FrontWorld).Rotation();
			const FSkeletonKey NewKey = UFlecsEntityLibrary::SpawnEntityFromDefinition(W, CapturedDef, MidPoint, Rot);

			// Step 4 — CONTINUATION on sim thread.
			Art->EnqueueCommand([Art, NewKey, FrontSnapTargetId, BackSnapTargetId,
				FrontPortIndex, BackPortIndex]()
			{
				flecs::world* Wp = Art->GetFlecsWorld();
				if (!Wp) return;
				flecs::world W = *Wp;

				flecs::entity SegE = Art->GetEntityForBarrageKey(NewKey);
				if (!SegE.is_alive())
				{
					UE_LOG(LogCrafting, Error,
						TEXT("[Transport] continuation: spawned segment not alive (key=0x%llX)"),
						(unsigned long long)NewKey.Obj);
					return;
				}

				FConnectorPlaced Placed;
				Placed.FrontSnapTargetId       = FrontSnapTargetId;
				Placed.BackSnapTargetId        = BackSnapTargetId;
				Placed.FrontSnappedToPortIndex = (uint8)FrontPortIndex;
				Placed.BackSnappedToPortIndex  = (uint8)BackPortIndex;
				Placed.NetworkId               = 0;
				SegE.set<FConnectorPlaced>(Placed);
				SegE.add<FTagConnectorPlaced>();
				SegE.remove<FTagPickupable>();
				SegE.remove<FTagItem>();

				// Freeze body to Static.
				if (UBarrageDispatch* Barrage = Art->GetBarrageDispatch())
				{
					if (const FBarrageBody* Body = SegE.try_get<FBarrageBody>())
					{
						FBLet Prim = Barrage->GetShapeRef(Body->BarrageKey);
						if (FBarragePrimitive::IsNotNull(Prim))
						{
							Barrage->SetBodyMotionType(Prim->KeyIntoBarrage,
								JPH::EMotionType::Static, /*bActivate=*/false);
						}
					}
				}

				// Commit reverse refs on station ports.
				flecs::entity FrontT = W.entity(static_cast<flecs::entity_t>(FrontSnapTargetId));
				flecs::entity BackT  = W.entity(static_cast<flecs::entity_t>(BackSnapTargetId));
				if (FrontT.is_alive())
				{
					if (FStationPorts* P = FrontT.try_get_mut<FStationPorts>())
					{
						if (P->Ports.IsValidIndex(FrontPortIndex))
							P->Ports[FrontPortIndex].ConnectedSegmentId = (int64)SegE.id();
					}
					FrontT.remove<FPendingConnectorPlace>();
				}
				if (BackT.is_alive())
				{
					if (FStationPorts* P = BackT.try_get_mut<FStationPorts>())
					{
						if (P->Ports.IsValidIndex(BackPortIndex))
							P->Ports[BackPortIndex].ConnectedSegmentId = (int64)SegE.id();
					}
					BackT.remove<FPendingConnectorPlace>();
				}

				if (auto* Sub = UFlecsCraftingTransportSubsystem::SelfPtr)
					Sub->QueueRebuild();

				UE_LOG(LogCrafting, Log,
					TEXT("[Transport] PLACED segment=%llu front=(%lld port=%d) back=(%lld port=%d)"),
					(unsigned long long)SegE.id(),
					FrontSnapTargetId, FrontPortIndex,
					BackSnapTargetId, BackPortIndex);
			});
		});
	});
}

void UFlecsCraftingLibrary::RequestConnectorDetach(UObject* WorldContextObject, FSkeletonKey SegmentKey)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || !SegmentKey.IsValid())
	{
		UE_LOG(LogCrafting, Verbose, TEXT("[Transport] RequestConnectorDetach: invalid input"));
		return;
	}

	Subsystem->EnqueueCommand([Subsystem, SegmentKey]()
	{
		flecs::entity SegE = GetEntityFromFlecsIdKey(Subsystem, SegmentKey);
		if (!SegE.is_alive() || !SegE.has<FTagConnectorPlaced>())
		{
			UE_LOG(LogCrafting, Verbose, TEXT("[Transport] DetachConnector: target invalid"));
			return;
		}

		const FConnectorPlaced Placed = SegE.get<FConnectorPlaced>();
		flecs::world W = SegE.world();

		// Clear reverse refs on both ends.
		auto ClearStationSide = [&W](int64 TargetId, uint8 PortIdx)
		{
			if (TargetId == 0 || PortIdx == 0xFF) return;
			flecs::entity T = W.entity(static_cast<flecs::entity_t>(TargetId));
			if (!T.is_alive()) return;
			if (FStationPorts* P = T.try_get_mut<FStationPorts>())
			{
				if (P->Ports.IsValidIndex(PortIdx))
					P->Ports[PortIdx].ConnectedSegmentId = 0;
			}
		};
		ClearStationSide(Placed.FrontSnapTargetId, Placed.FrontSnappedToPortIndex);
		ClearStationSide(Placed.BackSnapTargetId,  Placed.BackSnappedToPortIndex);

		// Restore item-form tags (order matches Phase 4 detach: tags + grace BEFORE body wake).
		SegE.remove<FTagConnectorPlaced>();
		SegE.remove<FConnectorPlaced>();
		SegE.add<FTagPickupable>();
		SegE.add<FTagItem>();
		SegE.add<FTagInteractable>();

		// Phase 4 helpers reuse: pickup grace 0.5s, restore Dynamic body, small impulse.
		FWorldItemInstance WI;
		WI.PickupGraceTimer  = 0.5f;
		WI.DespawnTimer      = -1.f;
		WI.DroppedByEntityId = 0;
		SegE.set<FWorldItemInstance>(WI);
		SegE.add<FTagDroppedItem>();
		FlecsMultiblockRuntime::RestorePartToDynamic(SegE);
		FlecsMultiblockRuntime::ApplyDetachImpulse(SegE, /*ImpulseCmS=*/200.f);

		if (auto* TransportSub = UFlecsCraftingTransportSubsystem::SelfPtr)
			TransportSub->QueueRebuild();

		UE_LOG(LogCrafting, Log,
			TEXT("[Transport] DETACHED segment=%llu (front=%lld back=%lld)"),
			(unsigned long long)SegE.id(),
			Placed.FrontSnapTargetId, Placed.BackSnapTargetId);
	});
}
