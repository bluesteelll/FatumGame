// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArtilleryDispatch.h"
#include "SkeletonTypes.h"

#include "UEventLogSystem.generated.h"

UENUM(BlueprintType, Blueprintable)
enum class E_EventLogType : uint8
{
	Died,
};

USTRUCT()
struct FArtilleryEvent
{
	GENERATED_BODY()
	
	int32 LogTime;
	int32 ExpiryTime;
	E_EventLogType Type;
	FSkeletonKey LoggingKey;
	FSkeletonKey OtherKey;
};

UCLASS()
class ARTILLERYRUNTIME_API UEventLogSubsystem : public UTickableWorldSubsystem, public ISkeletonLord, public ITickHeavy
{
	GENERATED_BODY()

public:
	static inline UEventLogSubsystem* SelfPtr = nullptr;
	constexpr static int OrdinateSeqKey = ORDIN::E_D_C::EventLogSystem;
	
	UEventLogSubsystem() : MyDispatch(nullptr), EventLog()
	{
	}

	virtual bool RegistrationImplementation() override;

	void ArtilleryTick() override;
	
	void LogEvent(E_EventLogType LogType, FSkeletonKey LoggingKey, FSkeletonKey Other = FSkeletonKey::Invalid());

	TArray<FArtilleryEvent> GetEventsOfType(E_EventLogType LogType) const;

	TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UEventLogSubsystem, STATGROUP_Tickables);
	}
	
protected:
	UArtilleryDispatch* MyDispatch;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	
private:
	TArray<FArtilleryEvent> EventLog;
};
