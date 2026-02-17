// Blueprint function library for Flecs ECS interaction operations.
// Handles dispatch of interaction actions (Pickup, Toggle, Destroy, CustomEvent)
// and single-use management. Game-thread safe — enqueues to simulation thread.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SkeletonTypes.h"
#include "FlecsInteractionTypes.h"
#include "GameplayTagContainer.h"
#include "FlecsInteractionLibrary.generated.h"

class UFlecsArtillerySubsystem;

/**
 * Callback fired on game thread when a container interaction is dispatched.
 * ContainerEntityId = Flecs entity id, InteractionTitle = cached prompt text.
 */
DECLARE_DELEGATE_TwoParams(FOnContainerOpened, int64 /*ContainerEntityId*/, const FText& /*InteractionTitle*/);

UCLASS()
class FATUMGAME_API UFlecsInteractionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// INSTANT ACTION DISPATCH (game-thread safe, enqueued to sim thread)
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Dispatch an instant interaction action on an entity.
	 * Pickup, Toggle, Destroy, CustomEvent execute on sim thread.
	 * OpenContainer fires OnContainerOpened delegate on game thread.
	 *
	 * @param WorldContextObject  World context for subsystem access.
	 * @param Action              Which instant action to perform.
	 * @param TargetKey           Barrage key of the target entity.
	 * @param InventoryEntityId   Player's inventory Flecs entity id (needed for Pickup).
	 * @param EventTag            Gameplay tag for CustomEvent action.
	 * @param InteractionTitle    Text shown in container UI (needed for OpenContainer).
	 * @param OnContainerOpened   Callback for OpenContainer action (game thread).
	 */
	static void DispatchInstantAction(
		UObject* WorldContextObject,
		EInstantAction Action,
		FSkeletonKey TargetKey,
		int64 InventoryEntityId = 0,
		FGameplayTag EventTag = FGameplayTag(),
		const FText& InteractionTitle = FText::GetEmpty(),
		FOnContainerOpened OnContainerOpened = FOnContainerOpened());

	/**
	 * Blueprint-friendly version for simple actions (no container callback).
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Interaction", meta = (WorldContext = "WorldContextObject"))
	static void ExecuteInteraction(
		UObject* WorldContextObject,
		EInstantAction Action,
		FSkeletonKey TargetKey,
		int64 InventoryEntityId,
		FGameplayTag EventTag);

	// ═══════════════════════════════════════════════════════════════
	// SINGLE-USE MANAGEMENT
	// ═══════════════════════════════════════════════════════════════

	/** If entity has FInteractionStatic::bSingleUse, removes FTagInteractable. Enqueued to sim thread. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Interaction", meta = (WorldContext = "WorldContextObject"))
	static void ApplySingleUseIfNeeded(UObject* WorldContextObject, FSkeletonKey TargetKey);

	// ═══════════════════════════════════════════════════════════════
	// LEGACY TAG-BASED DISPATCH
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Legacy interaction dispatch based on entity tags (backward compat).
	 * Checks FTagPickupable+FTagItem → pickup, FTagContainer → open container, else → log.
	 *
	 * @param WorldContextObject  World context.
	 * @param TargetKey           Barrage key of the target entity.
	 * @param InventoryEntityId   Player's inventory entity id.
	 * @param InteractionTitle    Cached prompt text for container UI.
	 * @param OnContainerOpened   Callback when container is opened (game thread).
	 */
	static void DispatchLegacyInteraction(
		UObject* WorldContextObject,
		FSkeletonKey TargetKey,
		int64 InventoryEntityId,
		const FText& InteractionTitle,
		FOnContainerOpened OnContainerOpened = FOnContainerOpened());

	// ═══════════════════════════════════════════════════════════════
	// CONTAINER INTERACTION (game thread callback via sim thread validation)
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Validate container entity on sim thread, then fire callback on game thread.
	 *
	 * @param WorldContextObject  World context.
	 * @param TargetKey           Barrage key of the container entity.
	 * @param InteractionTitle    Prompt text for container UI.
	 * @param OnContainerOpened   Callback fired on game thread.
	 */
	static void DispatchContainerInteraction(
		UObject* WorldContextObject,
		FSkeletonKey TargetKey,
		const FText& InteractionTitle,
		FOnContainerOpened OnContainerOpened);

	// ═══════════════════════════════════════════════════════════════
	// QUERY
	// ═══════════════════════════════════════════════════════════════

	/** Get the toggle state of an entity. Returns false if entity has no FInteractionInstance. */
	UFUNCTION(BlueprintPure, Category = "Flecs|Interaction", meta = (WorldContext = "WorldContextObject"))
	static bool GetToggleState(UObject* WorldContextObject, FSkeletonKey TargetKey);
};
