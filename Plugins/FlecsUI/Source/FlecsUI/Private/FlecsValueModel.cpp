// UFlecsValueModel — value ViewModel implementation.

#include "FlecsValueModel.h"
#include "IFlecsValueView.h"

float UFlecsValueModel::GetFloat(FName Key) const
{
	const float* Val = Floats.Find(Key);
	return Val ? *Val : 0.f;
}

int32 UFlecsValueModel::GetInt(FName Key) const
{
	const int32* Val = Ints.Find(Key);
	return Val ? *Val : 0;
}

bool UFlecsValueModel::GetBool(FName Key) const
{
	const bool* Val = Bools.Find(Key);
	return Val ? *Val : false;
}

void UFlecsValueModel::UpdateFloat(FName Key, float Value)
{
	float& Stored = Floats.FindOrAdd(Key, 0.f);
	if (Stored != Value)
	{
		const float OldValue = Stored;
		Stored = Value;
		NotifyViewsFloatChanged(Key, OldValue, Value);
	}
}

void UFlecsValueModel::UpdateInt(FName Key, int32 Value)
{
	int32& Stored = Ints.FindOrAdd(Key, 0);
	if (Stored != Value)
	{
		const int32 OldValue = Stored;
		Stored = Value;
		NotifyViewsIntChanged(Key, OldValue, Value);
	}
}

void UFlecsValueModel::UpdateBool(FName Key, bool Value)
{
	bool& Stored = Bools.FindOrAdd(Key, false);
	if (Stored != Value)
	{
		const bool OldValue = Stored;
		Stored = Value;
		NotifyViewsBoolChanged(Key, OldValue, Value);
	}
}

void UFlecsValueModel::Deactivate()
{
	Floats.Empty();
	Ints.Empty();
	Bools.Empty();
	Super::Deactivate();
}

void UFlecsValueModel::NotifyViewsFloatChanged(FName Key, float OldValue, float NewValue)
{
	for (auto& Weak : Views)
	{
		if (auto* Obj = Weak.Get())
		{
			if (auto* View = Cast<IFlecsValueView>(Obj))
			{
				View->OnFloatValueChanged(Key, OldValue, NewValue);
			}
		}
	}
}

void UFlecsValueModel::NotifyViewsIntChanged(FName Key, int32 OldValue, int32 NewValue)
{
	for (auto& Weak : Views)
	{
		if (auto* Obj = Weak.Get())
		{
			if (auto* View = Cast<IFlecsValueView>(Obj))
			{
				View->OnIntValueChanged(Key, OldValue, NewValue);
			}
		}
	}
}

void UFlecsValueModel::NotifyViewsBoolChanged(FName Key, bool OldValue, bool NewValue)
{
	for (auto& Weak : Views)
	{
		if (auto* Obj = Weak.Get())
		{
			if (auto* View = Cast<IFlecsValueView>(Obj))
			{
				View->OnBoolValueChanged(Key, OldValue, NewValue);
			}
		}
	}
}
