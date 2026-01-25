// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ArtilleryDispatch.h"
#include "KeyedConcept.h"
#include "ORDIN.h"
#include "Subsystems/WorldSubsystem.h"
#include "SunflowerDispatch.generated.h"

/**
 * Component for separating UI dependencies.
 */
UCLASS()
class SUNFLOWERRUNTIME_API USunflowerDispatch : public UTickableWorldSubsystem, public ISkeletonLord, public ICanReady
{
	GENERATED_BODY()
	USunflowerDispatch()
	{
	};

protected:
	constexpr static int OrdinateSeqKey = ORDIN::E_D_C::UIECSSystem;
	virtual bool RegistrationImplementation() override; 
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

public:

private:

};
