// Copyright Epic Games, Inc. All Rights Reserved.

// ReSharper disable CppRedundantParentheses
//they weren't fucking redundant.
#pragma once

#include "CoreMinimal.h"
#include "SkeletonTypes.h"
#include "FRelationshipMap.h"
#include "Constellation.generated.h"

//Unlike almost all other designs, the constellation is a concept that has no formal equivalent in most ECS systems
//It's a fully privileged entity-of-entities that may or may not have a type on top of that. Think of it as an Entity Facade
//with the actual entities being Whatever is stored in the Relationship Attribute Map. Or think of it as a db record with just foreign keys.
//We really don't want these to proliferate wildly, but they're very nice for understanding things like player state without
//breaking UE's expectations.
UINTERFACE()
class ARTILLERYRUNTIME_API UConstellation : public UKeyedConstruct
{
	GENERATED_UINTERFACE_BODY()
};

inline UConstellation::UConstellation(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

class IConstellation: public IKeyedConstruct
{
	GENERATED_IINTERFACE_BODY()
	
public:
	FRelationshipMap Entities;
	FConstellationKey Self;
	
	virtual FSkeletonKey GetMyKey() const override { return FSkeletonKey(Self.Obj); }
};
