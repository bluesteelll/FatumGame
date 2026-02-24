// Data Asset mapping Input Actions to GameplayTags.
// Single source of truth for all input bindings — replaces per-action UPROPERTY on character.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FatumInputConfig.generated.h"

class UInputAction;

/** Single mapping: InputAction <-> GameplayTag. */
USTRUCT(BlueprintType)
struct FFatumInputAction
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UInputAction> InputAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (Categories = "InputTag"))
	FGameplayTag InputTag;
};

/**
 * Data Asset holding all input action mappings for a character.
 *
 * Create in Content Browser: Data Asset -> FatumInputConfig
 * Assign to AFlecsCharacter::InputConfig.
 */
UCLASS(BlueprintType, Const)
class FATUMGAME_API UFatumInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Actions bound to specific C++ handlers (move, look, fire, etc.) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (TitleProperty = "InputTag"))
	TArray<FFatumInputAction> NativeInputActions;

	/** Find the InputAction for a given tag. Returns nullptr if not found. */
	const UInputAction* FindNativeInputActionForTag(const FGameplayTag& InputTag) const
	{
		for (const FFatumInputAction& Action : NativeInputActions)
		{
			if (Action.InputAction && Action.InputTag.MatchesTagExact(InputTag))
			{
				return Action.InputAction;
			}
		}
		return nullptr;
	}
};
