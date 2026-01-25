// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

#include "StateTreeTaskBase.h"		//this defines the basic form of an actual tree task.
#include "StateTreeExecutionContext.h"
#include "ThistleBehavioralist.h"
#include "ThistleTypes.h"
#include "ThistleStateTreeCore.h"
#include "ThistleStateTreeFire.generated.h"

using namespace ThistleTypes;

USTRUCT()
struct THISTLERUNTIME_API FFireTurret : public FTTaskBase
{
	GENERATED_BODY()
	using FInstanceDataType = F_ArtilleryKeyInstanceData;

protected:
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
};
