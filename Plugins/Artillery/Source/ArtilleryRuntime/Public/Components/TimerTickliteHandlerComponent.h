#pragma once

#include "TimerTickliteHandlerComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTickliteTick, int32, TicksRemaining);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTickliteExpire);

UCLASS()
class ARTILLERYRUNTIME_API UTimerTickliteHandlerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UTimerTickliteHandlerComponent(const FObjectInitializer& ObjectInitializer);
	
	
	void TickliteExpired();

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(BlueprintAssignable, Category = "Artillery|Ticklites")
	FOnTickliteTick OnTickliteTick;
	
	UPROPERTY(BlueprintAssignable, Category = "Artillery|Ticklites")
	FOnTickliteExpire OnTickliteExpire;

	void SetTicksRemaining(int32 NewLeft) { TicksRemaining = NewLeft; }

private:
	static const FName NAME_ActorFeatureName;
	
	int32 TicksRemaining;
	bool FlaggedForDelete;
};
