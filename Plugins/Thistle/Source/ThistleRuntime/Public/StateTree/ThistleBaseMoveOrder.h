#pragma once

#include "StateTreeTaskBase.h"	
#include "StateTreeExecutionContext.h"
#include "ThistleBehavioralist.h"
#include "ThistleStateTreeCore.h"
#include "ThistleTypes.h"

#include "ThistleBaseMoveOrder.generated.h"

USTRUCT()
struct THISTLERUNTIME_API FMoveOrder : public FTTaskBase
{
	GENERATED_BODY()
	
	using FInstanceDataType = F_TPOIInstanceNavData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	EStateTreeRunStatus AttemptMovePath(FStateTreeExecutionContext& Context, FVector location, FVector HereIAm) const;

	/**
	 * Called during state tree tick when the task is on active state.
	 * Note: The method is called only if bShouldCallTick or bShouldCallTickOnlyOnEvents is set.
	 * @param Context Reference to current execution context.
	 * @param DeltaTime Time since last StateTree tick.
	 * @return Running status of the state: Running if still in progress, Succeeded if execution is done and succeeded, Failed if execution is done and failed.
	 */
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	
	UPROPERTY(EditAnywhere)
	float Tolerance = 0;
};
