// UFlecsUIWidget — base for Flecs-connected UMG widgets.

#include "FlecsUIWidget.h"
#include "Blueprint/WidgetTree.h"

bool UFlecsUIWidget::Initialize()
{
	if (!Super::Initialize()) return false;

	// Build C++ fallback tree only if BP Designer didn't provide content.
	// BindWidgetOptional properties are already linked by UMG if BP provides them.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultWidgetTree();
	}

	PostInitialize();

	return true;
}

void UFlecsUIWidget::RefreshVisuals()
{
	OnUpdateVisuals();
}
