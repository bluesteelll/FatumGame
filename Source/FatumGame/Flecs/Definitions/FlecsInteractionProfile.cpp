#include "FlecsInteractionProfile.h"

#if WITH_EDITORONLY_DATA

void UFlecsInteractionProfile::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshEditorBools();
}

void UFlecsInteractionProfile::PostLoad()
{
	Super::PostLoad();
	RefreshEditorBools();
}

void UFlecsInteractionProfile::RefreshEditorBools()
{
	bIsInstant = (InteractionType == EInteractionType::Instant);
	bIsNotInstant = !bIsInstant;
	bIsFocus = (InteractionType == EInteractionType::Focus);
	bIsHold = (InteractionType == EInteractionType::Hold);
	bIsFocusCamera = bIsFocus && bMoveCamera;
	bIsCustomEvent = bIsInstant && (InstantAction == EInstantAction::CustomEvent);
	bIsHoldCustomEvent = bIsHold && (CompletionAction == EInstantAction::CustomEvent);
}

#endif
