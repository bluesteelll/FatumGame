// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "skeletonize.h"
#include "SkeletonTypes.h"
#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "Templates/SubclassOf.h"
#include "UObject/UnrealType.h"
#include "Engine/DataTable.h"

#include "Containers/CircularBuffer.h"
#include "FGunKey.generated.h"

USTRUCT(BlueprintType)
struct ARTILLERYRUNTIME_API FGunKey
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly)
	FString GunDefinitionID; //this will need to be human searchable
	//FUN STORY: BLUEPRINT CAN'T USE UINT64.
	FGunInstanceKey GunInstanceID;
	
	// ReSharper disable once CppRedundantMemberInitializer
	// THIS DOES NOT DO THE SAME THING AS INSTANCE 0
	FGunKey() : GunDefinitionID("M6D"), GunInstanceID()
	{
	}

	// ReSharper disable once CppRedundantMemberInitializer
	// THIS DOES NOT DO THE SAME THING AS INSTANCE 0
	FGunKey(const FString& Name) : GunDefinitionID(Name), GunInstanceID()
	{
	}
	
	explicit FGunKey(const FString& Name, FGunInstanceKey id) : GunDefinitionID(Name), GunInstanceID(id)
	{
	}
	
	FGunKey(const FString& Name, uint32_t id) : GunDefinitionID(Name), GunInstanceID(id)
	{
	}
	
	operator FSkeletonKey() const
	{
		return FSkeletonKey(FORGE_SKELETON_KEY(GetTypeHash(*this), SKELLY::SFIX_ART_GUNS));
	}
	
	friend uint32 GetTypeHash(const FGunKey Other)
	{
		//WARNING: TYPEHASH CONSIDERED HARMFUL
		//originally, we just used typehash.
		//it was extremely not fine. TypeHash is fucked for scalars of 4 bytes or less. don't use it.
		return HashCombineFast(GetTypeHash(Other.GunDefinitionID), FMMM::FastHash32(Other.GunInstanceID));
	}

	bool IsValidInstance() const { return GunInstanceID.Obj != 0; }

	static FGunKey Invalid() { return FGunKey(); }
};

static bool operator==(FGunKey const& lhs, FGunKey const& rhs) {
	return (lhs.GunDefinitionID == rhs.GunDefinitionID) && (lhs.GunInstanceID == rhs.GunInstanceID);
}

//when sorted, gunkeys follow their instantiation order!
static bool operator<(FGunKey const& lhs, FGunKey const& rhs) {
	return (lhs.GunInstanceID < rhs.GunInstanceID);
}

//see you soon, chief...
static const FGunKey DefaultGunKey = FGunKey("M6D");