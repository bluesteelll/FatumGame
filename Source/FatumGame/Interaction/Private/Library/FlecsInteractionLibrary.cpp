// UFlecsInteractionLibrary: dispatch layer for interaction actions.
// All functions are game-thread safe — sim-thread work is enqueued via EnqueueCommand.

#include "FlecsInteractionLibrary.h"
#include "FlecsLibraryHelpers.h"
#include "FlecsContainerLibrary.h"
#include "FlecsInteractionComponents.h"
#include "FlecsGameTags.h"
#include "Async/Async.h"

// ═══════════════════════════════════════════════════════════════
// INSTANT ACTION DISPATCH
// ═══════════════════════════════════════════════════════════════

void UFlecsInteractionLibrary::DispatchInstantAction(
	UObject* WorldContextObject,
	EInstantAction Action,
	FSkeletonKey TargetKey,
	int64 InventoryEntityId,
	FGameplayTag EventTag,
	const FText& InteractionTitle,
	FOnContainerOpened OnContainerOpened)
{
	UFlecsArtillerySubsystem* Sub = FlecsLibrary::GetSubsystem(WorldContextObject);
	check(Sub);
	checkf(TargetKey.IsValid(), TEXT("DispatchInstantAction: Invalid TargetKey"));

	// OpenContainer is game-thread only — validate on sim, callback on game
	if (Action == EInstantAction::OpenContainer)
	{
		if (OnContainerOpened.IsBound())
		{
			DispatchContainerInteraction(WorldContextObject, TargetKey, InteractionTitle, OnContainerOpened);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("DispatchInstantAction: OpenContainer requires OnContainerOpened callback"));
		}
		return;
	}

	Sub->EnqueueCommand([Sub, Action, EventTag, TargetKey, InventoryEntityId]()
	{
		flecs::entity Target = Sub->GetEntityForBarrageKey(TargetKey);
		if (!Target.is_valid() || Target.has<FTagDead>()) return;

		switch (Action)
		{
		case EInstantAction::Pickup:
		{
			if (!Target.has<FTagPickupable>() || !Target.has<FTagItem>())
			{
				UE_LOG(LogTemp, Warning, TEXT("InstantAction::Pickup: Entity %llu missing FTagPickupable/FTagItem"), Target.id());
				return;
			}
			if (InventoryEntityId == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("InstantAction::Pickup: No inventory"));
				return;
			}
			int32 PickedUp = 0;
			UFlecsContainerLibrary::PickupWorldItem(
				Sub, static_cast<int64>(Target.id()), InventoryEntityId, PickedUp);
			break;
		}

		case EInstantAction::Toggle:
		{
			FInteractionInstance* Inst = Target.try_get_mut<FInteractionInstance>();
			if (!Inst)
			{
				FInteractionInstance NewInst;
				NewInst.bToggleState = true;
				NewInst.UseCount = 1;
				Target.set<FInteractionInstance>(NewInst);
				UE_LOG(LogTemp, Log, TEXT("Interact: Toggle entity %llu -> ON (first use)"), Target.id());
			}
			else
			{
				Inst->bToggleState = !Inst->bToggleState;
				Inst->UseCount++;
				UE_LOG(LogTemp, Log, TEXT("Interact: Toggle entity %llu -> %s"),
					Target.id(), (Inst->bToggleState ? TEXT("ON") : TEXT("OFF")));
			}
			break;
		}

		case EInstantAction::Destroy:
			Target.add<FTagDead>();
			UE_LOG(LogTemp, Log, TEXT("Interact: Destroy entity %llu"), Target.id());
			break;

		case EInstantAction::OpenContainer:
			// Handled above (game thread), should never reach here
			checkNoEntry();
			break;

		case EInstantAction::CustomEvent:
			UE_LOG(LogTemp, Log, TEXT("Interact: CustomEvent '%s' on entity %llu"),
				*EventTag.ToString(), Target.id());
			// TODO: Route event via message system for Blueprint/system listeners
			break;
		}
	});
}

void UFlecsInteractionLibrary::ExecuteInteraction(
	UObject* WorldContextObject,
	EInstantAction Action,
	FSkeletonKey TargetKey,
	int64 InventoryEntityId,
	FGameplayTag EventTag)
{
	DispatchInstantAction(WorldContextObject, Action, TargetKey, InventoryEntityId, EventTag);
}

// ═══════════════════════════════════════════════════════════════
// SINGLE-USE
// ═══════════════════════════════════════════════════════════════

void UFlecsInteractionLibrary::ApplySingleUseIfNeeded(UObject* WorldContextObject, FSkeletonKey TargetKey)
{
	UFlecsArtillerySubsystem* Sub = FlecsLibrary::GetSubsystem(WorldContextObject);
	check(Sub);
	if (!TargetKey.IsValid()) return;

	Sub->EnqueueCommand([Sub, TargetKey]()
	{
		flecs::entity Target = Sub->GetEntityForBarrageKey(TargetKey);
		if (!Target.is_valid()) return;

		const FInteractionStatic* InterStatic = Target.try_get<FInteractionStatic>();
		if (InterStatic && InterStatic->bSingleUse)
		{
			Target.remove<FTagInteractable>();
		}
	});
}

// ═══════════════════════════════════════════════════════════════
// LEGACY TAG-BASED DISPATCH
// ═══════════════════════════════════════════════════════════════

void UFlecsInteractionLibrary::DispatchLegacyInteraction(
	UObject* WorldContextObject,
	FSkeletonKey TargetKey,
	int64 InventoryEntityId,
	const FText& InteractionTitle,
	FOnContainerOpened OnContainerOpened)
{
	UFlecsArtillerySubsystem* Sub = FlecsLibrary::GetSubsystem(WorldContextObject);
	check(Sub);
	if (!TargetKey.IsValid()) return;

	// Capture callback as shared ptr for cross-thread safety
	TSharedPtr<FOnContainerOpened> CallbackPtr;
	if (OnContainerOpened.IsBound())
	{
		CallbackPtr = MakeShared<FOnContainerOpened>(OnContainerOpened);
	}

	Sub->EnqueueCommand([Sub, TargetKey, InventoryEntityId, InteractionTitle, CallbackPtr]()
	{
		flecs::entity Target = Sub->GetEntityForBarrageKey(TargetKey);
		if (!Target.is_valid() || Target.has<FTagDead>() || !Target.has<FTagInteractable>())
		{
			return;
		}

		if (Target.has<FTagPickupable>() && Target.has<FTagItem>())
		{
			if (InventoryEntityId == 0) return;
			int32 PickedUp = 0;
			UFlecsContainerLibrary::PickupWorldItem(
				Sub, static_cast<int64>(Target.id()), InventoryEntityId, PickedUp);
		}
		else if (Target.has<FTagContainer>())
		{
			const int64 ContainerEntityId = static_cast<int64>(Target.id());
			UE_LOG(LogTemp, Log, TEXT("Interact: Open container %lld (Key=%llu, legacy path)"),
				ContainerEntityId, static_cast<uint64>(TargetKey));

			if (CallbackPtr.IsValid() && CallbackPtr->IsBound())
			{
				// Copy for game-thread capture
				TSharedPtr<FOnContainerOpened> GameThreadCallback = CallbackPtr;
				FText Title = InteractionTitle;
				AsyncTask(ENamedThreads::GameThread, [GameThreadCallback, ContainerEntityId, Title]()
				{
					GameThreadCallback->ExecuteIfBound(ContainerEntityId, Title);
				});
			}
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("Interact: Generic use entity %llu (legacy path)"), Target.id());
		}

		// Single-use
		if (!Target.has<FTagPickupable>())
		{
			const FInteractionStatic* InterStatic = Target.try_get<FInteractionStatic>();
			if (InterStatic && InterStatic->bSingleUse)
			{
				Target.remove<FTagInteractable>();
			}
		}
	});
}

// ═══════════════════════════════════════════════════════════════
// CONTAINER INTERACTION
// ═══════════════════════════════════════════════════════════════

void UFlecsInteractionLibrary::DispatchContainerInteraction(
	UObject* WorldContextObject,
	FSkeletonKey TargetKey,
	const FText& InteractionTitle,
	FOnContainerOpened OnContainerOpened)
{
	UFlecsArtillerySubsystem* Sub = FlecsLibrary::GetSubsystem(WorldContextObject);
	check(Sub);
	checkf(TargetKey.IsValid(), TEXT("DispatchContainerInteraction: Invalid TargetKey"));

	TSharedPtr<FOnContainerOpened> CallbackPtr = MakeShared<FOnContainerOpened>(OnContainerOpened);
	FText Title = InteractionTitle;

	Sub->EnqueueCommand([Sub, TargetKey, Title, CallbackPtr]()
	{
		flecs::entity Target = Sub->GetEntityForBarrageKey(TargetKey);
		if (!Target.is_valid() || Target.has<FTagDead>()) return;

		if (!Target.has<FTagContainer>())
		{
			UE_LOG(LogTemp, Warning, TEXT("DispatchContainerInteraction: Entity %llu is not a container"), Target.id());
			return;
		}

		const int64 ContainerEntityId = static_cast<int64>(Target.id());
		UE_LOG(LogTemp, Log, TEXT("Interact: Open container %lld (Key=%llu)"),
			ContainerEntityId, static_cast<uint64>(TargetKey));

		AsyncTask(ENamedThreads::GameThread, [CallbackPtr, ContainerEntityId, Title]()
		{
			if (CallbackPtr.IsValid())
			{
				CallbackPtr->ExecuteIfBound(ContainerEntityId, Title);
			}
		});
	});
}

// ═══════════════════════════════════════════════════════════════
// QUERY (game-thread) — CROSS-THREAD READ
// Values may be stale by 1-2 frames. Safe for UI/cosmetics.
// ═══════════════════════════════════════════════════════════════

bool UFlecsInteractionLibrary::GetToggleState(UObject* WorldContextObject, FSkeletonKey TargetKey)
{
	UFlecsArtillerySubsystem* Sub = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Sub || !Sub->GetFlecsWorld() || !TargetKey.IsValid()) return false;

	flecs::entity E = Sub->GetEntityForBarrageKey(TargetKey);
	if (!E.is_valid()) return false;

	const FInteractionInstance* Inst = E.try_get<FInteractionInstance>();
	return Inst ? Inst->bToggleState : false;
}
