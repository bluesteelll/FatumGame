// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Item and Container registry for fast lookup by TypeId.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ItemRegistry.generated.h"

class UFlecsItemDefinition;
class UFlecsContainerDefinition;

/**
 * Registry for item and container definitions.
 * Provides O(1) lookup by TypeId/DefinitionId.
 *
 * Auto-scans all Data Assets on initialization.
 * Singleton accessible via UGameInstance::GetSubsystem<UItemRegistry>().
 */
UCLASS()
class FATUMGAME_API UItemRegistry : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// LIFECYCLE
	// ═══════════════════════════════════════════════════════════════

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ═══════════════════════════════════════════════════════════════
	// REGISTRATION
	// ═══════════════════════════════════════════════════════════════

	/** Register an item definition. Called automatically on init. */
	UFUNCTION(BlueprintCallable, Category = "Item Registry")
	void RegisterItem(UFlecsItemDefinition* Definition);

	/** Register a container definition. Called automatically on init. */
	UFUNCTION(BlueprintCallable, Category = "Item Registry")
	void RegisterContainer(UFlecsContainerDefinition* Definition);

	/** Unregister an item by TypeId. */
	void UnregisterItem(int32 TypeId);

	/** Unregister a container by DefinitionId. */
	void UnregisterContainer(int32 DefinitionId);

	// ═══════════════════════════════════════════════════════════════
	// LOOKUP
	// ═══════════════════════════════════════════════════════════════

	/** Get item definition by TypeId. Returns nullptr if not found. */
	UFUNCTION(BlueprintPure, Category = "Item Registry")
	UFlecsItemDefinition* GetItemDefinition(int32 TypeId) const;

	/** Get container definition by DefinitionId. Returns nullptr if not found. */
	UFUNCTION(BlueprintPure, Category = "Item Registry")
	UFlecsContainerDefinition* GetContainerDefinition(int32 DefinitionId) const;

	/** Get item definition by name. Slower than by TypeId. */
	UFUNCTION(BlueprintPure, Category = "Item Registry")
	UFlecsItemDefinition* GetItemDefinitionByName(FName ItemName) const;

	/** Get container definition by name. Slower than by DefinitionId. */
	UFUNCTION(BlueprintPure, Category = "Item Registry")
	UFlecsContainerDefinition* GetContainerDefinitionByName(FName ContainerName) const;

	// ═══════════════════════════════════════════════════════════════
	// QUERIES
	// ═══════════════════════════════════════════════════════════════

	/** Check if item TypeId exists. */
	UFUNCTION(BlueprintPure, Category = "Item Registry")
	bool HasItem(int32 TypeId) const { return ItemDefinitions.Contains(TypeId); }

	/** Check if container DefinitionId exists. */
	UFUNCTION(BlueprintPure, Category = "Item Registry")
	bool HasContainer(int32 DefinitionId) const { return ContainerDefinitions.Contains(DefinitionId); }

	/** Get all registered item definitions. */
	UFUNCTION(BlueprintPure, Category = "Item Registry")
	TArray<UFlecsItemDefinition*> GetAllItemDefinitions() const;

	/** Get all registered container definitions. */
	UFUNCTION(BlueprintPure, Category = "Item Registry")
	TArray<UFlecsContainerDefinition*> GetAllContainerDefinitions() const;

	/** Get item count. */
	UFUNCTION(BlueprintPure, Category = "Item Registry")
	int32 GetItemCount() const { return ItemDefinitions.Num(); }

	/** Get container count. */
	UFUNCTION(BlueprintPure, Category = "Item Registry")
	int32 GetContainerCount() const { return ContainerDefinitions.Num(); }

	// ═══════════════════════════════════════════════════════════════
	// STATIC ACCESS
	// ═══════════════════════════════════════════════════════════════

	/** Get the registry from any world context. */
	UFUNCTION(BlueprintPure, Category = "Item Registry", meta = (WorldContext = "WorldContextObject"))
	static UItemRegistry* Get(const UObject* WorldContextObject);

private:
	/** Scan and register all Data Assets of type UFlecsItemDefinition */
	void ScanAndRegisterItems();

	/** Scan and register all Data Assets of type UFlecsContainerDefinition */
	void ScanAndRegisterContainers();

	UPROPERTY()
	TMap<int32, TObjectPtr<UFlecsItemDefinition>> ItemDefinitions;

	UPROPERTY()
	TMap<int32, TObjectPtr<UFlecsContainerDefinition>> ContainerDefinitions;

	// Name-to-ID maps for slower name lookups
	TMap<FName, int32> ItemNameToId;
	TMap<FName, int32> ContainerNameToId;
};
