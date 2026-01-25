#include "ThistleStateTreeSchema.h"
//avoid nasty circularities.
#include "ThistleEvaluators.h"
#include "ThistleStateTreeConditions.h"
#include "ThistleStateTreeCore.h"

bool UThistleStateTreeSchema::SetContextRequirements(FSkeletonKey KeyToSet, FStateTreeExecutionContext& Context, bool bLogErrors)
{
	auto Instance = new F_ArtilleryKeyInstanceData(KeyToSet);
	Context.SetContextDataByName(
		ThistleTypes::ContextKey,
		FStateTreeDataView(F_ArtilleryKeyInstanceData::StaticStruct(),
		reinterpret_cast<uint8*>(Instance)));
	
	bool bResult = Context.AreContextDataViewsValid();
	if (!bResult && bLogErrors)
	{
		UE_LOG(LogStateTree, Error, TEXT("Could not get key. External data requirements. StateTree will not update."));
	}
	return true;
}

bool UThistleStateTreeSchema::CollectExternalData(
	const FStateTreeExecutionContext& Context,
	F_ArtilleryKeyInstanceData* KeySource,
	TArrayView<const FStateTreeExternalDataDesc> Descs,
	TArrayView<FStateTreeDataView> OutDataViews)
{
	IKeyedConstruct* Owner = Cast<IKeyedConstruct>(Context.GetOwner());
	if (!Owner)
	{
		return false;
	}
	
	for (int32 Index = 0; Index < Descs.Num(); Index++)
	{
		const FStateTreeExternalDataDesc& ItemDesc = Descs[Index];
		if (ItemDesc.Struct != nullptr && ItemDesc.Struct->IsChildOf(F_ArtilleryKeyInstanceData::StaticStruct()))
		{
			OutDataViews[Index] = FStateTreeDataView(F_ArtilleryKeyInstanceData::StaticStruct(), reinterpret_cast<uint8*>(KeySource));
		}
	}
	return true;
}

TConstArrayView<FStateTreeExternalDataDesc> UThistleStateTreeSchema::GetContextDataDescs() const
{
	return ContextDataDescs;
}

bool UThistleStateTreeSchema::IsStructAllowed(const UScriptStruct* InScriptStruct) const
{
	return Super::IsStructAllowed(InScriptStruct)
		|| InScriptStruct->IsChildOf(FThistleMSEvaluator::StaticStruct())
		|| InScriptStruct->IsChildOf(FTTaskBase::StaticStruct())
		|| InScriptStruct->IsChildOf(FThistleCondition::StaticStruct());
}
