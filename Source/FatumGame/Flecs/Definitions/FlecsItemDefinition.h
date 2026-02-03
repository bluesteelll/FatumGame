// Data Asset for Flecs-based items.
//
// NOTE: This defines ITEM LOGIC only. For world presence (physics, rendering),
// use separate profiles: UFlecsPhysicsProfile, UFlecsRenderProfile.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FlecsItemDefinition.generated.h"

class UTexture2D;

/**
 * Item action type - what can be done with the item.
 */
UENUM(BlueprintType)
enum class EItemActionType : uint8
{
	None		UMETA(DisplayName = "None"),
	Use			UMETA(DisplayName = "Use"),
	Consume		UMETA(DisplayName = "Consume"),
	Equip		UMETA(DisplayName = "Equip"),
	Throw		UMETA(DisplayName = "Throw"),
	Place		UMETA(DisplayName = "Place in world"),
	Examine		UMETA(DisplayName = "Examine/Read")
};

/**
 * Single item action definition.
 */
USTRUCT(BlueprintType)
struct FItemAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	EItemActionType ActionType = EItemActionType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	FText ActionDisplayName;

	/** Gameplay tag sent when action is triggered (for handling in gameplay systems) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	FGameplayTag ActionTag;

	/** If true, this is the default action (double-click, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	bool bIsDefaultAction = false;

	/** If true, item is destroyed after this action */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	bool bConsumeOnUse = false;

	/** How many of the stack are consumed (for stackable items) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (EditCondition = "bConsumeOnUse", ClampMin = "1"))
	int32 ConsumeCount = 1;
};

/**
 * Data Asset defining an item type (LOGIC ONLY).
 *
 * Create in Content Browser: Right Click -> Miscellaneous -> Data Asset -> FlecsItemDefinition
 *
 * This defines item behavior:
 * - Identity (name, tags)
 * - Stacking rules
 * - Inventory size and weight
 * - Actions (use, consume, equip)
 *
 * For world presence, combine with:
 * - UFlecsPhysicsProfile (collision, physics)
 * - UFlecsRenderProfile (mesh, materials)
 *
 * Example spawn:
 *   FEntitySpawnRequest::At(Location)
 *       .WithItem(DA_Potion, 5)
 *       .WithPhysics(DA_SmallItemPhysics)
 *       .WithRender(DA_PotionMesh)
 *       .Pickupable()
 *       .Spawn(World);
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// IDENTITY
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Unique item type ID. Used for registry lookup and serialization.
	 * MUST be unique across all items. 0 = invalid (auto-generated from ItemName).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (ClampMin = "0"))
	int32 ItemTypeId = 0;

	/** Internal name for this item type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FName ItemName = "NewItem";

	/** Display name shown in UI */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FText DisplayName;

	/** Item description for tooltips */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity", meta = (MultiLine = true))
	FText Description;

	/** Gameplay tags for filtering and categorization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FGameplayTagContainer ItemTags;

	// ═══════════════════════════════════════════════════════════════
	// STACKING
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Maximum stack size.
	 * 0 = Unique item (not stackable, has FItemUniqueData for durability/enchants)
	 * 1 = Single item per slot
	 * >1 = Stackable up to this amount
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stacking", meta = (ClampMin = "0"))
	int32 MaxStackSize = 99;

	bool IsStackable() const { return MaxStackSize > 1; }
	bool IsUnique() const { return MaxStackSize == 0; }

	// ═══════════════════════════════════════════════════════════════
	// INVENTORY
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Grid size for 2D inventory (width x height in cells).
	 * Most items are 1x1, weapons might be 2x4, etc.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "1", ClampMax = "10"))
	FIntPoint GridSize = FIntPoint(1, 1);

	/** Item weight (for weight-limited containers) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "0"))
	float Weight = 0.1f;

	/** Base value (for trading, loot generation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "0"))
	float BaseValue = 0.f;

	/** Rarity tier (0 = common, higher = rarer) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "0"))
	int32 RarityTier = 0;

	// ═══════════════════════════════════════════════════════════════
	// UI
	// ═══════════════════════════════════════════════════════════════

	/** Icon for UI display */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	TObjectPtr<UTexture2D> Icon;

	/** Icon tint color */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	FLinearColor IconTint = FLinearColor::White;

	// ═══════════════════════════════════════════════════════════════
	// ACTIONS
	// ═══════════════════════════════════════════════════════════════

	/** Available actions for this item */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actions")
	TArray<FItemAction> Actions;

	/** Get the default action (first with bIsDefaultAction, or first action) */
	const FItemAction* GetDefaultAction() const
	{
		for (const FItemAction& Action : Actions)
		{
			if (Action.bIsDefaultAction)
			{
				return &Action;
			}
		}
		return Actions.Num() > 0 ? &Actions[0] : nullptr;
	}

	/** Check if item has specific action type */
	bool HasAction(EItemActionType Type) const
	{
		for (const FItemAction& Action : Actions)
		{
			if (Action.ActionType == Type)
			{
				return true;
			}
		}
		return false;
	}

	// ═══════════════════════════════════════════════════════════════
	// EQUIPMENT (only if HasAction(Equip))
	// ═══════════════════════════════════════════════════════════════

	/** Equipment slot this item goes into (if equippable) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Equipment", meta = (EditCondition = "MaxStackSize == 0"))
	FGameplayTag EquipmentSlot;

	// ═══════════════════════════════════════════════════════════════
	// DURABILITY (only for unique items)
	// ═══════════════════════════════════════════════════════════════

	/** Maximum durability (for unique items). 0 = indestructible */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Durability", meta = (EditCondition = "MaxStackSize == 0", ClampMin = "0"))
	float MaxDurability = 0.f;

	bool HasDurability() const { return IsUnique() && MaxDurability > 0.f; }

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId("FlecsItemDefinition", ItemName);
	}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);

		// Auto-generate TypeId if not set
		if (ItemTypeId == 0 && !ItemName.IsNone())
		{
			ItemTypeId = GetTypeHash(ItemName);
		}
	}
#endif
};
