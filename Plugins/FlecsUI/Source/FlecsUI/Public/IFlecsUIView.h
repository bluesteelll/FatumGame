// Base view interface for FlecsUI models.
// Concrete views (widgets) implement derived interfaces (IFlecsContainerView, IFlecsValueView).
// This base provides common view lifecycle.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IFlecsUIView.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UFlecsUIView : public UInterface
{
	GENERATED_BODY()
};

class FLECSUI_API IFlecsUIView
{
	GENERATED_BODY()
public:
	/** Called when the model is activated (entity bound). */
	virtual void OnModelActivated() {}

	/** Called when the model is deactivated (entity unbound). */
	virtual void OnModelDeactivated() {}
};
