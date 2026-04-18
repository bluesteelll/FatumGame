// Crafting recipe definition — ingredient list → output list + station gating.
// RecipeId is a manually-set FGuid (content-author responsibility, MJ5 Option A);
// IsDataValid errors when unset — no auto-generation.

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManager.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FlecsCraftingTypes.h"
#include "FlecsCraftingRecipeDef.generated.h"

class UTexture2D;

/**
 * One crafting recipe. Indexed by UFlecsCraftingRecipeRegistry at GameInstance::Initialize.
 *
 * Content-author responsibility:
 *  - RecipeId must be set via the editor "Generate New GUID" context action before saving.
 *    IsDataValid errors when unset; no auto-generation in PostLoad/PostInitProperties to avoid
 *    hidden mutations that diverge between editor / cooked builds (MJ5 Option A).
 *  - All ingredient + output UFlecsEntityDefinition soft refs must point to valid assets;
 *    registry drops unresolvable recipes at scan time with an Error log.
 */
UCLASS(BlueprintType)
class FATUMGAME_API UFlecsCraftingRecipeDef : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// IDENTITY
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Stable recipe identifier. MUST be set manually by the content author
	 * (right-click the FGuid property → "Generate" in the editor).
	 * Lexicographic order on RecipeId.ToString() breaks ties on ambiguous matches.
	 */
	UPROPERTY(EditAnywhere, Category = "ID")
	FGuid RecipeId;

	UPROPERTY(EditAnywhere, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, Category = "Identity", meta = (MultiLine = true))
	FText Description;

	UPROPERTY(EditAnywhere, Category = "Identity")
	TObjectPtr<UTexture2D> Icon;

	// ═══════════════════════════════════════════════════════════════
	// RECIPE DATA
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, Category = "Recipe")
	TArray<FCraftingIngredient> Ingredients;

	UPROPERTY(EditAnywhere, Category = "Recipe")
	TArray<FCraftingOutput> Outputs;

	/** Fuel type required by this recipe. None = no fuel check. */
	UPROPERTY(EditAnywhere, Category = "Recipe")
	EFuelType RequiredFuelType = EFuelType::Coal;

	/** How many seconds of fuel reservoir this recipe consumes. */
	UPROPERTY(EditAnywhere, Category = "Recipe", meta = (ClampMin = "0"))
	float FuelChargeSecondsRequired = 10.f;

	/** Wall-clock processing time (sim-thread phase 3 consumes). */
	UPROPERTY(EditAnywhere, Category = "Recipe", meta = (ClampMin = "0"))
	float DurationSeconds = 10.f;

	// ═══════════════════════════════════════════════════════════════
	// GATING
	// ═══════════════════════════════════════════════════════════════

	/** Which station types this recipe runs on. */
	UPROPERTY(EditAnywhere, Category = "Gating")
	TSet<ECraftingStationType> CompatibleStations;

	/** Optional tag query applied against FCraftingStationStatic::StationTag. */
	UPROPERTY(EditAnywhere, Category = "Gating")
	FGameplayTagQuery StationTagQuery;

	// ═══════════════════════════════════════════════════════════════
	// ASSET MANAGER HOOK
	// ═══════════════════════════════════════════════════════════════

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(FPrimaryAssetType(TEXT("FlecsCraftingRecipe")), GetFName());
	}

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
