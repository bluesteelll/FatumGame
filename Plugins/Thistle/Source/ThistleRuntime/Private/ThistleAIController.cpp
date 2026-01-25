#include "ThistleAIController.h"

// Sets default values
AThistleAIController::AThistleAIController()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AThistleAIController::BeginPlay()
{
	Super::BeginPlay();
	SetPathFollowingComponent(nullptr);
	SetActorTickInterval(0.5);
}

// Called every frame
void AThistleAIController::Tick(float DeltaTime)
{
	if (IsActorTickEnabled())
	{
		Super::Tick(DeltaTime);
	}
}

bool AThistleAIController::IsLocalController() const
{
	return true;
}

