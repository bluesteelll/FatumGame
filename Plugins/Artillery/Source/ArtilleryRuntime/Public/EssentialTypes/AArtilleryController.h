// Copyright Epic Games, Inc. All Rights Reserved.

// ReSharper disable CppRedundantParentheses
//they weren't fucking redundant.
#pragma once

#include "CoreMinimal.h"
#include "ArtilleryShell.h"
#include "KeyCarry.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Interface.h"
#include "GameFramework/Actor.h"
#include "UObject/ScriptMacros.h"
#include "ArtilleryActorControllerConcepts.h"
#include "AArtilleryController.generated.h"


//You will need to settle on what gets registered where and how dispatch registration occurs.
//in theory, any dispatch could own an artillery controller. practically, only artillery and possibly thistle seem
//likely to own them. still, to prevent dependency messes, we leave that ill defined in this class.
//See ABarragePlayerController for an implementation that actually fleshes out the necessary stuff.
UCLASS()
class ARTILLERYRUNTIME_API AArtilleryController : public APlayerController, public IArtilleryControllite
{
	GENERATED_BODY()

public:
	bool ShouldArtilleryTick = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Identity)
	//It's quite important that controllers don't have keycarries, as those set up a transform linkage we don't want.
	FSkeletonKey MyObjectKey;
	
	virtual void ArtilleryTick(FArtilleryShell PreviousMovement, FArtilleryShell Movement, bool RunAtLeastOnce, bool Smear) override {}
	virtual void RegisterWithDispatch(FSkeletonKey MyKey) override {} // we can't really implement this here without making the dependency tree delicate.

	//Override this.
	virtual void BeginPlay() override
	{
		Super::BeginPlay();
	}
	
	AArtilleryController()
	{
		uint32 val = PointerHash(GetOwner()); //this is the standard construction of the actor key. we'll need to revise it sooner or later.
		ActorKey TopLevelActorKey = ActorKey(val);
		MyObjectKey = TopLevelActorKey;
	}
	
	virtual FSkeletonKey GetMyKey() const override { return MyObjectKey; }
};