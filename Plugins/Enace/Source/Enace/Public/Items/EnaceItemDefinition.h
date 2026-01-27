// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "EPhysicsLayer.h"
#include "EnaceItemTypes.h"
#include "EnaceItemDefinition.generated.h"

class UStaticMesh;
class UTexture2D;

/**
 * Data asset defining an item type.
 * Create these in Content Browser: Right-click -> Miscellaneous -> Data Asset -> EnaceItemDefinition
 */
UCLASS(BlueprintType, Blueprintable)
class ENACE_API UEnaceItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// IDENTITY
	// ═══════════════════════════════════════════════════════════════

	/** Internal ID for this item type */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FName ItemId;

	/** Display name shown to player */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	/** Description shown in UI */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = true))
	FText Description;

	/** Item category */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	EEnaceItemCategory Category = EEnaceItemCategory::Misc;

	/** Item rarity */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	EEnaceItemRarity Rarity = EEnaceItemRarity::Common;

	/** Gameplay tags for filtering and behavior */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FGameplayTagContainer ItemTags;

	// ═══════════════════════════════════════════════════════════════
	// STACKING
	// ═══════════════════════════════════════════════════════════════

	/** Maximum items per stack (1 = non-stackable) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stacking", meta = (ClampMin = "1", ClampMax = "9999"))
	int32 MaxStackSize = 99;

	/** Weight per single item (for inventory limits) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stacking", meta = (ClampMin = "0"))
	float Weight = 1.0f;

	// ═══════════════════════════════════════════════════════════════
	// VISUALS (for future UI/rendering)
	// ═══════════════════════════════════════════════════════════════

	/** Icon for inventory UI */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visuals")
	TObjectPtr<UTexture2D> Icon;

	/** Mesh for world representation */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visuals")
	TObjectPtr<UStaticMesh> WorldMesh;

	/** Scale for world mesh */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visuals")
	FVector WorldMeshScale = FVector::OneVector;

	/** Color tint based on rarity (used by UI and effects) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visuals")
	FLinearColor RarityColor = FLinearColor::White;

	// ═══════════════════════════════════════════════════════════════
	// BARRAGE PHYSICS (Jolt)
	// ═══════════════════════════════════════════════════════════════
	// NOTE: Item rendering uses Instanced Static Mesh via UBarrageRenderManager.
	// All items with the same mesh share a single draw call.

	/** Physics layer for collision filtering */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physics")
	EPhysicsLayer PhysicsLayer = EPhysicsLayer::DEBRIS;

	/** Auto-calculate collider from mesh bounds */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physics")
	bool bAutoCollider = true;

	/** Manual collider size (if bAutoCollider = false) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physics",
		meta = (EditCondition = "!bAutoCollider"))
	FVector ColliderSize = FVector(20, 20, 20);

	/** Gravity factor (0 = floating, 1 = normal) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0", ClampMax = "2"))
	float GravityFactor = 1.0f;

	/** Bounciness when hitting surfaces */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0", ClampMax = "1"))
	float Restitution = 0.3f;

	/** Friction coefficient */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0", ClampMax = "1"))
	float Friction = 0.5f;

	// ═══════════════════════════════════════════════════════════════
	// CONTAINER (for items that can hold other items)
	// ═══════════════════════════════════════════════════════════════

	/** Is this item a container (chest, bag, etc.)? */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Container")
	bool bIsContainer = false;

	/** Number of slots when used as container (0 = not a container) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Container",
		meta = (EditCondition = "bIsContainer", ClampMin = "0", ClampMax = "1000"))
	int32 ContainerCapacity = 0;

	/** Allow nested containers (bags inside this container) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Container",
		meta = (EditCondition = "bIsContainer"))
	bool bAllowNestedContainers = true;

	/** Default slot type filters (applied to all slots) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Container",
		meta = (EditCondition = "bIsContainer"))
	FGameplayTagContainer DefaultSlotFilters;

	// ═══════════════════════════════════════════════════════════════
	// WORLD BEHAVIOR
	// ═══════════════════════════════════════════════════════════════

	/** Pickup radius in world units */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World", meta = (ClampMin = "10"))
	float PickupRadius = 100.0f;

	/** Can be picked up by walking over */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World")
	bool bAutoPickup = true;

	/** Physics enabled when in world */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World")
	bool bPhysicsEnabled = true;

	/** Default despawn time in seconds (0 = never) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World", meta = (ClampMin = "0"))
	float DefaultDespawnTime = 300.0f;

	// ═══════════════════════════════════════════════════════════════
	// API
	// ═══════════════════════════════════════════════════════════════

	/** Get total weight for a stack of this item */
	UFUNCTION(BlueprintPure, Category = "Enace|Item")
	float GetStackWeight(int32 Count) const { return Weight * Count; }

	/** Check if this item can stack with another definition */
	UFUNCTION(BlueprintPure, Category = "Enace|Item")
	bool CanStackWith(const UEnaceItemDefinition* Other) const;

	/** Get color for rarity (utility for default colors) */
	UFUNCTION(BlueprintPure, Category = "Enace|Item")
	static FLinearColor GetDefaultRarityColor(EEnaceItemRarity InRarity);

	// UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
