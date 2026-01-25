// Fill out your copyright notice in the Description page of Project Settings.

//In general, you should not need this with the switch to state trees.
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ThistleAIController.generated.h"

UCLASS()
class THISTLERUNTIME_API AThistleAIController : public AAIController
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AThistleAIController();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	//Thistle is _always_ run locally, as it simulates using ONLY player input streams.
	virtual bool IsLocalController() const override;
};
