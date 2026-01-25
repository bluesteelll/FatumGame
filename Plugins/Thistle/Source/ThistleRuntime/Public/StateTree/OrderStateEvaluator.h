#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"
#include "ThistleTypes.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "ThistleEvaluators.h"
#include "OrderStateEvaluator.generated.h"

USTRUCT(meta = (DisplayName = "Get Player Key"))
struct THISTLERUNTIME_API FOrderStateEvaluator : public FThistleMSEvaluator
{
	GENERATED_BODY()

	using FInstanceDataType = F_KOutOnlyInstanceData;
	
protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

