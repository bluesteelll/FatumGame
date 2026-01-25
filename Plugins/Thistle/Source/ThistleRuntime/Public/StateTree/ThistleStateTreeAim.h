#pragma once

#include "StateTreeTaskBase.h"		//this defines the basic form of an actual tree task.
#include "StateTreeExecutionContext.h"
#include "ThistleBehavioralist.h"
#include "ThistleTypes.h"
#include "ThistleStateTreeCore.h"

#include "ThistleStateTreeAim.generated.h"

using namespace ThistleTypes;

USTRUCT()
struct THISTLERUNTIME_API FAimTurret : public FTTaskBase
{
	GENERATED_BODY()
	using FInstanceDataType = F_TPOIInstanceData;

protected:
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
};
