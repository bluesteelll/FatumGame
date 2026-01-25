// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "UObject/UnrealType.h"
#include "Engine/DataTable.h"
#include "FMasks.h"
#include <bitset>
#include "MashFunctions.h"
#include "Containers/CircularBuffer.h"
#include "FActionBitMask.generated.h"

//we INTENTIONALLY lose our connection to the bristlecone
//type system here.
USTRUCT(BlueprintType)
struct FActionBitMask
{
	GENERATED_BODY()
	
public:
	std::bitset<Arty::Intents::TYPEBREAK_MAPPING_FROM_BC_BUTTONS> buttons;
	
	uint32_t getFlat()
	{
		uint32_t result = buttons.to_ulong();
		return (result);
	}
	
	friend uint32 GetTypeHash(const FActionBitMask& Other)
	{
		// it's probably fine!
		// update: it wasn't fine. don't use typehash unless you're sure it's actually hashing and always double check.
		uint32_t result = Other.buttons.to_ulong();
		return  FMMM::FastHash32(result);
	}

	static FActionBitMask Default()
	{
		FActionBitMask ActionBitMask;
		ActionBitMask.buttons.reset();
		return ActionBitMask;
	}

	FORCEINLINE bool operator==(const FActionBitMask& Other) const
	{
		return buttons.to_ulong() == Other.buttons.to_ulong();
	}
};
