// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "UObject/UnrealType.h"
#include "Engine/DataTable.h"
#include "SkeletonTypes.h"
#include "Containers/CircularBuffer.h"
#include "ConservedKey.generated.h"

static constexpr uint32 CONSERVED_ATTRIBUTE_BUFFER_SIZE = 128;

/**
 * Conserved key attributes record their last CONSERVED_ATTRIBUTE_BUFFER_SIZE changes.
 * Currently, this is for debug purposes, but it will be necessary for rollback.
 */
USTRUCT(BlueprintType)
struct ARTILLERYRUNTIME_API FConservedAttributeKey
{
	GENERATED_BODY()
	
	TCircularBuffer<FSkeletonKey> CurrentHistory = TCircularBuffer<FSkeletonKey>(CONSERVED_ATTRIBUTE_BUFFER_SIZE);
	TCircularBuffer<FSkeletonKey> RemoteHistory = TCircularBuffer<FSkeletonKey>(CONSERVED_ATTRIBUTE_BUFFER_SIZE);
	TCircularBuffer<FSkeletonKey> BaseHistory = TCircularBuffer<FSkeletonKey>(CONSERVED_ATTRIBUTE_BUFFER_SIZE);

	UPROPERTY(BlueprintReadOnly, Category = "Attribute")
	FSkeletonKey BaseValue;

	UPROPERTY(BlueprintReadOnly, Category = "Attribute")
	FSkeletonKey CurrentValue;

	void SetCurrentValue(FSkeletonKey NewValue) {
		CurrentHistory[CurrentHistory.GetNextIndex(CurrentHead)] = CurrentValue;
		CurrentValue = NewValue;
		++CurrentHead;
	}
	
	void SetRemoteValue(FSkeletonKey NewValue) {
		RemoteHistory[RemoteHistory.GetNextIndex(RemoteHead)] = NewValue;
		++RemoteHead;
	}
	
	void SetBaseValue(FSkeletonKey NewValue) {
		BaseHistory[BaseHistory.GetNextIndex(BaseHead)] = BaseValue;
		BaseValue = NewValue;
		++BaseHead;
	}

protected:
	uint64_t BaseHead = 0;
	uint64_t CurrentHead = 0;
	uint64_t RemoteHead = 0;
};

