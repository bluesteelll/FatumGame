// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

#include "StateTreeTaskBase.h"		//this defines the basic form of an actual tree task.
#include "StateTreeExecutionContext.h"
#include "ThistleBehavioralist.h"
#include "ThistleTypes.h"
#include "ThistleStateTreeCore.h"
#include "ThistleStateTreeScoot.generated.h"

using namespace ThistleTypes;

//Scoots away whenever distance to PoI is less than tolerance on a slow cadence.
USTRUCT()
struct THISTLERUNTIME_API FScoot : public FTTaskBase
{
	GENERATED_BODY()
	using FInstanceDataType = F_TPOIInstanceNavData;
	
	UPROPERTY(EditAnywhere)
	float Tolerance = 200;

protected:
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	EStateTreeRunStatus AttemptScootPath(FStateTreeExecutionContext& Context, FVector location, FVector HereIAm) const;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
};

USTRUCT()
struct THISTLERUNTIME_API FBreakOff : public FTTaskBase
{
	GENERATED_BODY()
	using FInstanceDataType = F_TPOIInstanceNavData;
	
	UPROPERTY(EditAnywhere)
	float Tolerance = 200;

protected:
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
};