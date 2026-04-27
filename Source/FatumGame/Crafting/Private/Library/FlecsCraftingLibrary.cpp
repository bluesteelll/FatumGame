// UFlecsCraftingLibrary — Blueprint-facing crafting API.

#include "FlecsCraftingLibrary.h"
#include "FlecsCraftingRuntime.h"
#include "Components/FlecsCraftingComponents.h"
#include "FlecsCraftingLog.h"
#include "FlecsCraftingUISubsystem.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsLibraryHelpers.h"
#include "FlecsItemComponents.h"
#include "flecs.h"
#include "Async/Async.h"

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
