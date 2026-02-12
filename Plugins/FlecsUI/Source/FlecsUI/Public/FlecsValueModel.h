// Value ViewModel for FlecsUI.
// Caches simple values (health, ammo, etc.) by FName keys.
// Updated by subsystem from packed atomics on game thread Tick.
// Zero-latency reads for widgets.

#pragma once

#include "CoreMinimal.h"
#include "FlecsUIModel.h"
#include "FlecsValueModel.generated.h"

class IFlecsValueView;

UCLASS()
class FLECSUI_API UFlecsValueModel : public UFlecsUIModel
{
	GENERATED_BODY()

public:
	// ═══ Queries (instant, local cache — game thread only) ═══

	float GetFloat(FName Key) const;
	int32 GetInt(FName Key) const;
	bool GetBool(FName Key) const;

	// ═══ Updates (called by subsystem after atomic unpack) ═══

	/** Update a float value. Notifies views only if changed. */
	void UpdateFloat(FName Key, float Value);

	/** Update an int value. Notifies views only if changed. */
	void UpdateInt(FName Key, int32 Value);

	/** Update a bool value. Notifies views only if changed. */
	void UpdateBool(FName Key, bool Value);

	// ═══ Lifecycle ═══

	virtual void Deactivate() override;

private:
	void NotifyViewsFloatChanged(FName Key, float OldValue, float NewValue);
	void NotifyViewsIntChanged(FName Key, int32 OldValue, int32 NewValue);
	void NotifyViewsBoolChanged(FName Key, bool OldValue, bool NewValue);

	TMap<FName, float> Floats;
	TMap<FName, int32> Ints;
	TMap<FName, bool> Bools;
};
