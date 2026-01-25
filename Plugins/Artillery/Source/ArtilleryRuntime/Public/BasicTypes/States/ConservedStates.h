// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "UObject/UnrealType.h"
#include "Engine/DataTable.h"
#include "Containers/CircularBuffer.h"

#include "ConservedStates.generated.h"

/**
 * Conserved states group flags and record their last 128 changes as a group. This will be superseded by gameplay tags
 * around January of 25, but right now, we aren't totally sure how we want tag replication to work, so this lets us defer.
 */
//all fields are now 4 bits. Fight me.
constexpr static uint64_t FIELDMASK_4bits =	0x000000000000000F; //1111
constexpr static uint64_t FIELDMASK_all =	0xFFFFFFFFFFFFFFFF;

//SWITCH TO GAMEPLAYTAGS using the AtomicTagArray!
//DO NOT MAKE FURTHER USE OF THIS unless you are _positive_ it's the best fit.
USTRUCT(BlueprintType)
struct ARTILLERYRUNTIME_API FConservedStateData 
{
	//I WANTED TO USE BLOODY BITFIELDS BUT NOOOOOOOO
	//NOPE. THEY DON'T FUCKING BLUEPRINT WELL AND THEY CAN'T BE AGGREGATED EASILY TO STORE THEM IN THE BUFFER.
	//SO HERE WE BLOODY ARE. SWITCH TO GAMEPLAYTAGS.
	//--J
	GENERATED_BODY()
	virtual ~FConservedStateData();
	FConservedStateData()
	{
		CurrentValue = 0;
		RemoteValue = 0;
	}

	enum class F
	{
		F1 = 0,
		F2 = 1,
		F3 = 2,
		F4 = 3,
		F5 = 4,
		F6 = 5,
		F7 = 6,
		F8 = 7,
		F9 = 8,
		F10 = 9,
		F11 = 10,
		F12 = 11,
		F13 = 12,
		F14 = 13,
		F15 = 14,
		F16 = 15
	};	
	
	TCircularBuffer<uint64_t> CurrentHistory = TCircularBuffer<uint64_t>(128);
	TCircularBuffer<uint64_t> RemoteHistory = TCircularBuffer<uint64_t>(128);
	//base is always zero
	uint64_t CurrentValue;
	uint64_t RemoteValue;

	virtual void SetField(F offsetBy, char valueFor) {
		CurrentHistory[CurrentHistory.GetNextIndex(CurrentHead)] = CurrentValue;
		unsigned long long mask = FIELDMASK_4bits << static_cast<unsigned long long>(offsetBy)*4;
		mask = ~mask;
		CurrentValue &= mask;
		CurrentValue |= (valueFor << static_cast<unsigned long long>(offsetBy)*4);
		++CurrentHead;
	};

	virtual unsigned long long GetField(F field)
	{
		unsigned long long offsetBy = static_cast<unsigned long long>(field)*4;
		return (CurrentValue & (FIELDMASK_4bits << offsetBy)) >> offsetBy;
	}

protected:
	uint64_t CurrentHead = 0;
	uint64_t RemoteHead = 0;
#undef FCSDMAKEFIELD
};

inline FConservedStateData::~FConservedStateData()
{
}
