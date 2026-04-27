// UFlecsCraftingRecipeDef — IsDataValid implementation.

#include "FlecsCraftingRecipeDef.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_EDITOR
EDataValidationResult UFlecsCraftingRecipeDef::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (!RecipeId.IsValid())
	{
		Context.AddError(FText::FromString(TEXT(
			"RecipeId is unset. Right-click the RecipeId field in the editor and pick 'Generate' to create a stable GUID.")));
		Result = EDataValidationResult::Invalid;
	}

	if (Ingredients.Num() == 0)
	{
		Context.AddError(FText::FromString(TEXT("Ingredients is empty — at least one ingredient required.")));
		Result = EDataValidationResult::Invalid;
	}

	for (int32 i = 0; i < Ingredients.Num(); ++i)
	{
		const FCraftingIngredient& Ing = Ingredients[i];
		if (Ing.IngredientDefinition.IsNull())
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Ingredient %d has null IngredientDefinition."), i)));
			Result = EDataValidationResult::Invalid;
		}
		if (Ing.Count < 1)
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Ingredient %d has Count < 1."), i)));
			Result = EDataValidationResult::Invalid;
		}
	}

	if (Outputs.Num() == 0)
	{
		Context.AddError(FText::FromString(TEXT("Outputs is empty — at least one output required.")));
		Result = EDataValidationResult::Invalid;
	}

	for (int32 i = 0; i < Outputs.Num(); ++i)
	{
		const FCraftingOutput& Out = Outputs[i];
		if (Out.OutputDefinition.IsNull())
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Output %d has null OutputDefinition."), i)));
			Result = EDataValidationResult::Invalid;
		}
		if (Out.Count < 1)
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Output %d has Count < 1."), i)));
			Result = EDataValidationResult::Invalid;
		}
	}

	if (DurationSeconds < 0.f)
	{
		Context.AddError(FText::FromString(TEXT("DurationSeconds must be >= 0.")));
		Result = EDataValidationResult::Invalid;
	}

	// Phase 3 invariant: a single-charge Smelter must be able to finish on the fuel
	// it commits at Start. Mid-process refuels are out of Phase 3 scope.
	if (FuelChargeSecondsRequired > DurationSeconds)
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("FuelChargeSecondsRequired (%.2f) > DurationSeconds (%.2f) — recipe cannot complete on a single fuel commit."),
			FuelChargeSecondsRequired, DurationSeconds)));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif // WITH_EDITOR
