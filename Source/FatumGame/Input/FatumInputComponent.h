// Enhanced Input Component with tag-based action binding.
// Looks up InputAction from UFatumInputConfig by GameplayTag, then calls standard BindAction.

#pragma once

#include "CoreMinimal.h"
#include "EnhancedInputComponent.h"
#include "FatumInputConfig.h"
#include "FatumInputComponent.generated.h"

UCLASS()
class FATUMGAME_API UFatumInputComponent : public UEnhancedInputComponent
{
	GENERATED_BODY()

public:
	/**
	 * Bind a native action by GameplayTag.
	 * Looks up the UInputAction* from Config, then calls EnhancedInputComponent::BindAction.
	 *
	 * Usage:
	 *   InputComp->BindNativeAction(Config, Tag_Input_Move, ETriggerEvent::Triggered, this, &ThisClass::Move);
	 */
	template <class UserClass, typename FuncType>
	void BindNativeAction(const UFatumInputConfig* Config, const FGameplayTag& InputTag,
		ETriggerEvent TriggerEvent, UserClass* Object, FuncType Func)
	{
		check(Config);
		const UInputAction* IA = Config->FindNativeInputActionForTag(InputTag);
		if (ensureMsgf(IA, TEXT("BindNativeAction: InputTag '%s' not found in InputConfig '%s'"),
			*InputTag.ToString(), *GetNameSafe(Config)))
		{
			BindAction(IA, TriggerEvent, Object, Func);
		}
	}
};
