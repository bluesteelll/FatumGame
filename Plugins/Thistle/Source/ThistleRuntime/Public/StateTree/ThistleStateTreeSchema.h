// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

//"IPropertyAccessEditor.h"
//"StateTreeEditorPropertyBindings.h" // this defines interfaces for data collection on bindables
//"StateTreeEditorData.h"	//this provides the editor-side implementation of the Property Owner
#include "MassStateTreeSchema.h"
#include "StateTreeExecutionContext.h"
#include "ThistleTypes.h"
#include "ThistleStateTreeSchema.generated.h"

/**
 * StateTree for Mass behaviors.
 */
UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "Thistle Director", CommonSchema))
class THISTLERUNTIME_API UThistleStateTreeSchema : public UMassStateTreeSchema
{
	GENERATED_BODY()

public:
	explicit UThistleStateTreeSchema() : ContextDataDescs()
	{
		//I've given it a guid based on some of my favorite things! Weird old music, steaks, my work, and Nic Cage.
		ContextDataDescs.Emplace(ThistleTypes::ContextKey, F_ArtilleryKeyInstanceData::StaticStruct(), FGuid(0xABBA, 0xD3ADB33F, 0xBADC0DE, 0xFACE0FF));
	}

	static bool SetContextRequirements(FSkeletonKey KeyToSet, FStateTreeExecutionContext& Context, bool bLogErrors = false);
	static bool CollectExternalData(const FStateTreeExecutionContext& Context, F_ArtilleryKeyInstanceData* KeySource, TArrayView<const
	                                FStateTreeExternalDataDesc> Descs, TArrayView<FStateTreeDataView> OutDataViews);
	virtual TConstArrayView<FStateTreeExternalDataDesc> GetContextDataDescs() const override;
	
	UPROPERTY()
	TArray<FStateTreeExternalDataDesc> ContextDataDescs;
	
protected:
	virtual bool IsStructAllowed(const UScriptStruct* InScriptStruct) const override;
};
