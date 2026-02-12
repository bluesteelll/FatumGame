// View interface for value-based UI (health bars, ammo counters, etc.).
// Widgets implement this to receive value change notifications.

#pragma once

#include "CoreMinimal.h"
#include "IFlecsUIView.h"
#include "IFlecsValueView.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UFlecsValueView : public UFlecsUIView
{
	GENERATED_BODY()
};

class FLECSUI_API IFlecsValueView : public IFlecsUIView
{
	GENERATED_BODY()

public:
	/** Called when a float value changed (e.g. "CurrentHP", "MaxHP"). */
	virtual void OnFloatValueChanged(FName Key, float OldValue, float NewValue) {}

	/** Called when an int value changed (e.g. "CurrentAmmo", "MagazineSize"). */
	virtual void OnIntValueChanged(FName Key, int32 OldValue, int32 NewValue) {}

	/** Called when a bool value changed (e.g. "bReloading"). */
	virtual void OnBoolValueChanged(FName Key, bool OldValue, bool NewValue) {}
};
