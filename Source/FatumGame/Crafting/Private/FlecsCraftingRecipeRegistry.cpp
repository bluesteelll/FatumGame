// UFlecsCraftingRecipeRegistry — scan + pre-resolve + station-type bucketing.

#include "FlecsCraftingRecipeRegistry.h"
#include "FlecsCraftingRecipeDef.h"
#include "FlecsCraftingLog.h"
#include "FlecsEntityDefinition.h"
#include "FlecsItemDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

// ═══════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════

void UFlecsCraftingRecipeRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogCrafting, Log, TEXT("CraftingRecipeRegistry initializing..."));

	ScanAndRegisterRecipes();
	PreResolveAllIngredients();
	RebuildIndices();

	// Deterministic tiebreak: sort each bucket by RecipeId.ToString() so FindMatch's
	// "first match" is reproducible across runs.
	for (auto& Pair : RecipesByStationType)
	{
		Pair.Value.Sort([](const UFlecsCraftingRecipeDef& A, const UFlecsCraftingRecipeDef& B)
		{
			return A.RecipeId.ToString() < B.RecipeId.ToString();
		});
	}

	UE_LOG(LogCrafting, Log, TEXT("CraftingRecipeRegistry initialized: %d recipes indexed across %d station types"),
		AllRecipes.Num(), RecipesByStationType.Num());
}

void UFlecsCraftingRecipeRegistry::Deinitialize()
{
	AllRecipes.Empty();
	RecipesByStationType.Empty();
	RecipesById.Empty();

	Super::Deinitialize();
}

// ═══════════════════════════════════════════════════════════════
// LOOKUP
// ═══════════════════════════════════════════════════════════════

const TArray<const UFlecsCraftingRecipeDef*>& UFlecsCraftingRecipeRegistry::GetRecipesForStationType(ECraftingStationType Type) const
{
	static const TArray<const UFlecsCraftingRecipeDef*> Empty;
	if (const TArray<const UFlecsCraftingRecipeDef*>* Found = RecipesByStationType.Find(Type))
	{
		return *Found;
	}
	return Empty;
}

const UFlecsCraftingRecipeDef* UFlecsCraftingRecipeRegistry::FindByRecipeId(const FGuid& Id) const
{
	if (const UFlecsCraftingRecipeDef* const* Found = RecipesById.Find(Id))
	{
		return *Found;
	}
	return nullptr;
}

const UFlecsCraftingRecipeDef* UFlecsCraftingRecipeRegistry::FindMatch(
	ECraftingStationType StationType,
	EFuelType FuelType,
	const TArray<TPair<int32, int32>>& Inputs,
	float FuelCharge) const
{
	const TArray<const UFlecsCraftingRecipeDef*>& Bucket = GetRecipesForStationType(StationType);
	if (Bucket.Num() == 0) return nullptr;

	for (const UFlecsCraftingRecipeDef* Def : Bucket)
	{
		if (!Def) continue;

		// ── Fuel gating ──────────────────────────────────────────
		// Recipe requires a specific fuel type; caller supplies the active (or available) fuel
		// type. Mismatch rejects; charge must meet requirement when fuel is required.
		if (Def->RequiredFuelType != EFuelType::None)
		{
			if (Def->RequiredFuelType != FuelType) continue;
			if (FuelCharge < Def->FuelChargeSecondsRequired) continue;
		}

		// ── Ingredient gating ────────────────────────────────────
		// Every recipe ingredient must be satisfied by some (ItemTypeId, Count>=Required) pair in Inputs.
		// Naive O(I*R) — ingredient lists are tiny (typical <=4).
		bool bAllIngredientsMatched = true;
		for (const FCraftingIngredient& Ing : Def->Ingredients)
		{
			const UFlecsEntityDefinition* ResolvedDef = Ing.ResolvedDefinition.Get();
			if (!ResolvedDef || !ResolvedDef->ItemDefinition)
			{
				// Registry should have dropped unresolvable recipes; defensive log + skip.
				bAllIngredientsMatched = false;
				break;
			}
			const int32 RequiredTypeId = ResolvedDef->ItemDefinition->ItemTypeId;
			const int32 RequiredCount  = Ing.Count;

			bool bFoundInInputs = false;
			for (const TPair<int32, int32>& Pair : Inputs)
			{
				if (Pair.Key == RequiredTypeId && Pair.Value >= RequiredCount)
				{
					bFoundInInputs = true;
					break;
				}
			}
			if (!bFoundInInputs)
			{
				bAllIngredientsMatched = false;
				break;
			}
		}

		if (bAllIngredientsMatched)
		{
			return Def;
		}
	}

	return nullptr;
}

UFlecsCraftingRecipeRegistry* UFlecsCraftingRecipeRegistry::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;

	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return nullptr;

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance) return nullptr;

	return GameInstance->GetSubsystem<UFlecsCraftingRecipeRegistry>();
}

// ═══════════════════════════════════════════════════════════════
// SCAN + RESOLVE
// ═══════════════════════════════════════════════════════════════

void UFlecsCraftingRecipeRegistry::ScanAndRegisterRecipes()
{
	UAssetManager& AssetManager = UAssetManager::Get();

	TArray<FAssetData> AssetList;
	const FPrimaryAssetType RecipeType("FlecsCraftingRecipe");

	AssetManager.GetPrimaryAssetDataList(RecipeType, AssetList);

	if (AssetList.Num() == 0)
	{
		// Fallback: class-filter scan via AssetRegistry (matches UItemRegistry pattern).
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		FARFilter Filter;
		Filter.ClassPaths.Add(UFlecsCraftingRecipeDef::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;

		AssetRegistry.GetAssets(Filter, AssetList);
	}

	AllRecipes.Reserve(AssetList.Num());
	for (const FAssetData& AssetData : AssetList)
	{
		if (UFlecsCraftingRecipeDef* RecipeDef = Cast<UFlecsCraftingRecipeDef>(AssetData.GetAsset()))
		{
			AllRecipes.Add(RecipeDef);
		}
	}
}

void UFlecsCraftingRecipeRegistry::PreResolveAllIngredients()
{
	// Iterate back-to-front so RemoveAt is O(1) amortised and indices remain valid.
	for (int32 i = AllRecipes.Num() - 1; i >= 0; --i)
	{
		UFlecsCraftingRecipeDef* Def = AllRecipes[i];
		if (!Def)
		{
			AllRecipes.RemoveAt(i);
			continue;
		}
		if (!Def->RecipeId.IsValid())
		{
			UE_LOG(LogCrafting, Error,
				TEXT("Recipe '%s' has unset RecipeId — drop. Generate the GUID in the Content Browser."),
				*Def->GetName());
			AllRecipes.RemoveAt(i);
			continue;
		}
		if (!TryResolveRecipe(Def))
		{
			UE_LOG(LogCrafting, Error,
				TEXT("Recipe '%s' has unresolvable ingredient/output — drop."),
				*Def->GetName());
			AllRecipes.RemoveAt(i);
			continue;
		}
	}
}

bool UFlecsCraftingRecipeRegistry::TryResolveRecipe(UFlecsCraftingRecipeDef* Def)
{
	if (!Def) return false;

	if (Def->Ingredients.Num() == 0)
	{
		UE_LOG(LogCrafting, Error, TEXT("Recipe '%s' has no ingredients"), *Def->GetName());
		return false;
	}
	if (Def->Outputs.Num() == 0)
	{
		UE_LOG(LogCrafting, Error, TEXT("Recipe '%s' has no outputs"), *Def->GetName());
		return false;
	}

	for (FCraftingIngredient& Ing : Def->Ingredients)
	{
		if (Ing.IngredientDefinition.IsNull())
		{
			UE_LOG(LogCrafting, Error, TEXT("Recipe '%s' ingredient has null soft pointer"), *Def->GetName());
			return false;
		}
		UFlecsEntityDefinition* Resolved = Ing.IngredientDefinition.LoadSynchronous();
		if (!Resolved)
		{
			UE_LOG(LogCrafting, Error, TEXT("Recipe '%s' ingredient failed to load: %s"),
				*Def->GetName(), *Ing.IngredientDefinition.ToString());
			return false;
		}
		if (!Resolved->ItemDefinition)
		{
			UE_LOG(LogCrafting, Error, TEXT("Recipe '%s' ingredient '%s' has no ItemDefinition"),
				*Def->GetName(), *Resolved->GetName());
			return false;
		}
		Ing.ResolvedDefinition = Resolved;
	}

	for (FCraftingOutput& Out : Def->Outputs)
	{
		if (Out.OutputDefinition.IsNull())
		{
			UE_LOG(LogCrafting, Error, TEXT("Recipe '%s' output has null soft pointer"), *Def->GetName());
			return false;
		}
		UFlecsEntityDefinition* Resolved = Out.OutputDefinition.LoadSynchronous();
		if (!Resolved)
		{
			UE_LOG(LogCrafting, Error, TEXT("Recipe '%s' output failed to load: %s"),
				*Def->GetName(), *Out.OutputDefinition.ToString());
			return false;
		}
		Out.ResolvedDefinition = Resolved;
	}

	return true;
}

void UFlecsCraftingRecipeRegistry::RegisterRecipe(UFlecsCraftingRecipeDef* Def)
{
	if (!Def) return;
	if (!Def->RecipeId.IsValid()) return;

	if (RecipesById.Contains(Def->RecipeId))
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("Duplicate RecipeId %s: '%s' conflicts with '%s' — skipping duplicate"),
			*Def->RecipeId.ToString(), *Def->GetName(),
			*RecipesById[Def->RecipeId]->GetName());
		return;
	}
	RecipesById.Add(Def->RecipeId, Def);

	for (ECraftingStationType Type : Def->CompatibleStations)
	{
		RecipesByStationType.FindOrAdd(Type).Add(Def);
	}

	UE_LOG(LogCrafting, Verbose, TEXT("Registered recipe: %s (Id: %s)"),
		*Def->GetName(), *Def->RecipeId.ToString());
}

void UFlecsCraftingRecipeRegistry::RebuildIndices()
{
	RecipesByStationType.Empty();
	RecipesById.Empty();
	for (const TObjectPtr<UFlecsCraftingRecipeDef>& Def : AllRecipes)
	{
		RegisterRecipe(Def.Get());
	}
}
