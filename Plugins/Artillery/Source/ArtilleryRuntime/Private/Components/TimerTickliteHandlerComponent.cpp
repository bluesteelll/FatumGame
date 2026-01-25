#include "Components/TimerTickliteHandlerComponent.h"

const FName UTimerTickliteHandlerComponent::NAME_ActorFeatureName("TimerTickliteHandler");

UTimerTickliteHandlerComponent::UTimerTickliteHandlerComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	TicksRemaining = -1;
	PrimaryComponentTick.bCanEverTick = true;
	FlaggedForDelete = false;
}

void UTimerTickliteHandlerComponent::TickliteExpired()
{
	UE_LOG(LogTemp, Warning, TEXT("Timer ticklite expired!"));
	OnTickliteExpire.Broadcast();
	FlaggedForDelete = true;
	OnTickliteExpire.Clear();
	OnTickliteTick.Clear();
}

void UTimerTickliteHandlerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	OnTickliteTick.Broadcast(TicksRemaining);

	if (FlaggedForDelete)
	{
		DestroyComponent();
	}
}

