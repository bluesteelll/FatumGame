// UFlecsUIModel — base model implementation.

#include "FlecsUIModel.h"
#include "IFlecsUIView.h"

void UFlecsUIModel::BindView(UObject* View)
{
	if (!View) return;

	// Avoid duplicates
	for (const auto& Existing : Views)
	{
		if (Existing.Get() == View) return;
	}

	Views.Add(View);
}

void UFlecsUIModel::UnbindView(UObject* View)
{
	if (!View) return;
	Views.RemoveAll([View](const TWeakObjectPtr<UObject>& Ptr) { return !Ptr.IsValid() || Ptr.Get() == View; });
}

void UFlecsUIModel::Activate(flecs::entity InEntity)
{
	Entity = InEntity;

	// Notify views
	for (auto& Weak : Views)
	{
		if (auto* Obj = Weak.Get())
		{
			if (auto* View = Cast<IFlecsUIView>(Obj))
			{
				View->OnModelActivated();
			}
		}
	}
}

void UFlecsUIModel::Deactivate()
{
	Entity = flecs::entity();
	PendingOps.Empty();

	// Notify views
	for (auto& Weak : Views)
	{
		if (auto* Obj = Weak.Get())
		{
			if (auto* View = Cast<IFlecsUIView>(Obj))
			{
				View->OnModelDeactivated();
			}
		}
	}
}

void UFlecsUIModel::ReceiveOpResult(uint32 OperationId, EUIOpResult Result)
{
	CompletePendingOp(OperationId, Result);
}

uint32 UFlecsUIModel::AllocateOpId()
{
	return NextOpId++;
}

bool UFlecsUIModel::CompletePendingOp(uint32 OpId, EUIOpResult Result)
{
	for (int32 i = 0; i < PendingOps.Num(); ++i)
	{
		if (PendingOps[i].OpId == OpId)
		{
			auto OnComplete = MoveTemp(PendingOps[i].OnComplete);
			PendingOps.RemoveAtSwap(i);

			if (OnComplete)
			{
				OnComplete(Result);
			}
			return true;
		}
	}
	return false;
}
