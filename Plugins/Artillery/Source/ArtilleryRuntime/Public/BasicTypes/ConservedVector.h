// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "UObject/UnrealType.h"
#include "Engine/DataTable.h"
#include "Containers/CircularBuffer.h"

#include "ConservedVector.generated.h"

/**
 * Conserved attributes record their last 128 changes.
 * Currently, this is for debug purposes, but we can use it with some additional features to provide a really expressive
 * model for rollback at a SUPER granular level if needed. 
 */
USTRUCT(BlueprintType)
struct ARTILLERYRUNTIME_API FConservedVector
{
	GENERATED_BODY()
	virtual ~FConservedVector() = default;
	TCircularBuffer<FVector3d> CurrentHistory = TCircularBuffer<FVector3d>(64);
	TCircularBuffer<FVector3d> RemoteHistory = TCircularBuffer<FVector3d>(64);

	virtual void SetCurrentValue(FVector3d NewValue) {
		CurrentHistory[CurrentHistory.GetNextIndex(CurrentHead)] = CurrentValue;
		CurrentValue = NewValue;
		++CurrentHead;
	};
	
	virtual void SetRemoteValue(FVector3d NewValue) {
		RemoteHistory[RemoteHistory.GetNextIndex(RemoteHead)] = NewValue;
		++RemoteHead;
	};

	FVector3d CurrentValue = FVector3d::ZeroVector;
	FVector3d RemoteValue = FVector3d::ZeroVector;
	
	uint64_t CurrentHead = 0;
	uint64_t RemoteHead = 0;
};

