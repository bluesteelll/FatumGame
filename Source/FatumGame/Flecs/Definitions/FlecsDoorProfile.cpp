#include "FlecsDoorProfile.h"

#if WITH_EDITORONLY_DATA

void UFlecsDoorProfile::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshEditorBools();
}

void UFlecsDoorProfile::PostLoad()
{
	Super::PostLoad();
	RefreshEditorBools();
}

void UFlecsDoorProfile::RefreshEditorBools()
{
	bIsHinged = (DoorType == EFlecsDoorType::Hinged);
	bIsSliding = (DoorType == EFlecsDoorType::Sliding);
}

#endif
