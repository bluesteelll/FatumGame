#include "FlecsAbilityDefinition.h"

#if WITH_EDITORONLY_DATA

void UFlecsAbilityDefinition::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshEditorBools();
}

void UFlecsAbilityDefinition::PostLoad()
{
	Super::PostLoad();
	RefreshEditorBools();
}

void UFlecsAbilityDefinition::RefreshEditorBools()
{
	bIsKineticBlast = (AbilityType == EAbilityType::KineticBlast);
}

#endif
