// Blueprint function library for Flecs ECS container operations.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SkeletonTypes.h"
#include "FlecsContainerLibrary.generated.h"

class UFlecsEntityDefinition;

UCLASS()
class UFlecsContainerLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// ITEM OPERATIONS (game-thread safe, enqueued to simulation thread)
	// ═══════════════════════════════════════════════════════════════

	/** Set despawn timer on an item entity. -1 = never despawns. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Item", meta = (WorldContext = "WorldContextObject"))
	static void SetItemDespawnTimer(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Timer);

	// ═══════════════════════════════════════════════════════════════
	// CONTAINER OPERATIONS (game-thread safe, enqueued to simulation thread)
	// Uses Flecs entity ID - no SkeletonKey.
	// ═══════════════════════════════════════════════════════════════

	UFUNCTION(BlueprintCallable, Category = "Flecs|Container", meta = (WorldContext = "WorldContextObject"))
	static bool AddItemToContainer(
		UObject* WorldContextObject,
		int64 ContainerEntityId,
		UFlecsEntityDefinition* EntityDefinition,
		int32 Count,
		int32& OutActuallyAdded,
		bool bAutoStack = true);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Container", meta = (WorldContext = "WorldContextObject"))
	static bool RemoveItemFromContainer(
		UObject* WorldContextObject,
		int64 ContainerEntityId,
		int64 ItemEntityId,
		int32 Count = -1);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Container", meta = (WorldContext = "WorldContextObject"))
	static int32 RemoveAllItemsFromContainer(
		UObject* WorldContextObject,
		int64 ContainerEntityId);

	UFUNCTION(BlueprintPure, Category = "Flecs|Container", meta = (WorldContext = "WorldContextObject"))
	static int32 GetContainerItemCount(
		UObject* WorldContextObject,
		int64 ContainerEntityId);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Container", meta = (WorldContext = "WorldContextObject"))
	static bool PickupItem(
		UObject* WorldContextObject,
		FSkeletonKey WorldItemKey,
		int64 ContainerEntityId,
		int32& OutPickedUp);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Container", meta = (WorldContext = "WorldContextObject"))
	static FSkeletonKey DropItem(
		UObject* WorldContextObject,
		int64 ContainerEntityId,
		int64 ItemEntityId,
		FVector DropLocation,
		int32 Count = -1);

};
