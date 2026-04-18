// Recipe registry — GameInstanceSubsystem. Scans all UFlecsCraftingRecipeDef
// assets at Initialize(), pre-resolves every TSoftObjectPtr<UFlecsEntityDefinition>
// via LoadSynchronous on the GAME THREAD (MJ4 — sim thread never loads assets).
//
// Hard-owned via UPROPERTY TArray<TObjectPtr<...>> → GC-rooted for the full game session.
// FCraftingIngredient::ResolvedDefinition and FCraftingOutput::ResolvedDefinition are
// filled in at scan time; sim-thread MatchRecipe() reads ONLY these fields.
//
// Mirrors UItemRegistry scan pattern.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FlecsCraftingTypes.h"
#include "FlecsCraftingRecipeRegistry.generated.h"

class UFlecsCraftingRecipeDef;

/**
 * Registry for crafting recipe definitions.
 *
 * Provides station-type bucket lookup + recipe-id lookup + first-match search.
 * Singleton accessible via UGameInstance::GetSubsystem<UFlecsCraftingRecipeRegistry>().
 */
UCLASS()
class FATUMGAME_API UFlecsCraftingRecipeRegistry : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// LIFECYCLE
	// ═══════════════════════════════════════════════════════════════

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ═══════════════════════════════════════════════════════════════
	// LOOKUP
	// ═══════════════════════════════════════════════════════════════

	/** All recipes registered against the given station type, sorted by RecipeId.ToString(). */
	const TArray<const UFlecsCraftingRecipeDef*>& GetRecipesForStationType(ECraftingStationType Type) const;

	/** O(1) lookup by stable FGuid RecipeId. Returns nullptr when unknown. */
	const UFlecsCraftingRecipeDef* FindByRecipeId(const FGuid& Id) const;

	/** Total distinct recipe count across all station buckets. */
	int32 GetTotalRecipeCount() const { return AllRecipes.Num(); }

	/**
	 * First-match search over the station-type bucket.
	 *
	 * @param StationType  Caller's station classification.
	 * @param FuelType     Currently-active fuel reservoir type (or fuel-item type when reservoir empty).
	 * @param Inputs       Pair<ItemTypeId, Count> for MaterialInput slots (sorted ascending by TypeId).
	 * @param FuelCharge   Seconds of fuel reservoir available.
	 *
	 * Returns first matching recipe by sorted-bucket order, or nullptr.
	 * Called ONLY from sim thread via FlecsCraftingRuntime::MatchRecipe.
	 */
	const UFlecsCraftingRecipeDef* FindMatch(
		ECraftingStationType StationType,
		EFuelType FuelType,
		const TArray<TPair<int32, int32>>& Inputs,
		float FuelCharge) const;

	// ═══════════════════════════════════════════════════════════════
	// STATIC ACCESS
	// ═══════════════════════════════════════════════════════════════

	/** Get the registry from any world context. */
	static UFlecsCraftingRecipeRegistry* Get(const UObject* WorldContextObject);

private:
	/** Discover recipe assets via AssetManager primary-asset query + class-filter fallback. */
	void ScanAndRegisterRecipes();

	/** Hard-resolve every soft ingredient/output on every recipe. Drops unresolvable entries. */
	void PreResolveAllIngredients();

	/** Resolve the soft pointers on a single recipe. Returns false on any unresolvable entry. */
	bool TryResolveRecipe(UFlecsCraftingRecipeDef* Def);

	/** Add a validated recipe to the by-station-type + by-id indices. */
	void RegisterRecipe(UFlecsCraftingRecipeDef* Def);

	/** Rebuild RecipesByStationType / RecipesById from AllRecipes after resolve-loop drops. */
	void RebuildIndices();

	/** Hard refs — GC-rooted for the full game session. */
	UPROPERTY()
	TArray<TObjectPtr<UFlecsCraftingRecipeDef>> AllRecipes;

	/** Station-type bucket index. Values point into AllRecipes (valid for session lifetime). */
	TMap<ECraftingStationType, TArray<const UFlecsCraftingRecipeDef*>> RecipesByStationType;

	/** Stable-id index. Values point into AllRecipes (valid for session lifetime). */
	TMap<FGuid, const UFlecsCraftingRecipeDef*> RecipesById;
};
