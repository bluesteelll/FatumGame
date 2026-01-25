// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

//"IPropertyAccessEditor.h"
//"StateTreeEditorPropertyBindings.h" // this defines interfaces for data collection on bindables
//"StateTreeEditorData.h"	//this provides the editor-side implementation of the Property Owner

#include "StateTreeTaskBase.h"		//this defines the basic form of an actual tree task.
#include "StateTreeExecutionContext.h"
#include "ThistleBehavioralist.h"
#include "ThistleStateTreeSchema.h"
#include "ThistleTypes.h"
#include "Components/StateTreeComponent.h"
//"StateTreePropertyRef.h"	//this is the ref used to bind properties.
//"StateTreePropertyRefHelpers.h" // here are some "helpers" for refs.
//reference matter can be found at the bottom.
#include "ThistleStateTreeCore.generated.h"
//StateTreeTaskBlueprintBase

//as always, the tests are helpful: StateTreeTestTypes.h
//A ton of the actual stuff is over in the GAMEPLAY statetrees module.
//UE_5.4\Engine\Plugins\Runtime\GameplayStateTree
//Lost a bit of time to that, so if you're looking for code and ref matter, do check there too.
//--J

using namespace ThistleTypes;

USTRUCT(meta = (Hidden))
struct THISTLERUNTIME_API FTTaskBase : public FStateTreeTaskBase
{
	GENERATED_BODY()
};

USTRUCT()
struct THISTLERUNTIME_API FStoreRelationship : public FTTaskBase
{
	GENERATED_BODY()
	using FInstanceDataType = F_SetRelatedKey;

protected:
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
};

USTRUCT()
struct THISTLERUNTIME_API FSetTagOfKey : public FTTaskBase
{
	GENERATED_BODY()
	using FInstanceDataType = F_TTagInstanceData;
	
protected:
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
};

USTRUCT()
struct THISTLERUNTIME_API FRemoveTagFromKey : public FTTaskBase
{
	GENERATED_BODY()
	using FInstanceDataType = F_TTagInstanceData;
	
protected:
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
};

USTRUCT()
struct THISTLERUNTIME_API FStoreToAttribute : public FTTaskBase
{
	GENERATED_BODY()
	using FInstanceDataType = F_TAttributeSetData;
	
protected:
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
};

////////////////////////////////////////////////////////////////////////
// Thistle State Tree Lease is a Cadenced StateTree holder. It provides registration and execution context support
// but ultimately it is a is a two-sided actor component that connects to the Thistle Behavioralist ECS Pillar.
// This allows Thistle Director StateTrees to access both the UE presentation state and the Artillery deterministic tick.
// Like other two-sided components, it is a TickHeavy, which implements the Artillery Tick method.

UCLASS(Blueprintable, ClassGroup = AI, HideCategories = (Activation, Collision), meta = (BlueprintSpawnableComponent))
class THISTLERUNTIME_API UThistleStateTreeLease : public UStateTreeComponent, public ITickHeavy
{
	GENERATED_BODY()
	
public:
	virtual void BeginDestroy() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	virtual void OnUnregister() override;

public:
	EStateTreeRunStatus CurrentRunStatus;
	UPROPERTY(EditAnywhere, Category = Parameter)
	F_ArtilleryKeyInstanceData InstanceOwnerKey;

#if WITH_GAMEPLAY_DEBUGGER
	virtual FString GetDebugInfoString() const override;
#endif // WITH_GAMEPLAY_DEBUGGER
	
	virtual void InitializeComponent() override;
	FOnCollectStateTreeExternalData BindDel;
	virtual bool CollectExternalData(const FStateTreeExecutionContext& Context, const UStateTree* StateTree, TArrayView<const FStateTreeExternalDataDesc> Descs, TArrayView<FStateTreeDataView> OutDataViews) const override;
	virtual void OnRegister() override;
	virtual bool RegistrationImplementation() override;
	virtual void OnClusterMarkedAsPendingKill() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	UThistleStateTreeLease(const FObjectInitializer& ObjectInitializer);

protected:
	virtual ~UThistleStateTreeLease() override;

public:
	virtual void BeginPlay() override;

	virtual FSkeletonKey GetMyKey() const override;

	// we may finally need to shim our executor via IGameplayTaskOwnerInterface 
	virtual UGameplayTasksComponent* GetGameplayTasksComponent(const UGameplayTask& Task) const override;

protected:
	virtual bool SetContextRequirements(FStateTreeExecutionContext& Context, bool bLogErrors = false) override;

public:
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	virtual void ArtilleryTick(uint64_t TicksSoFar) override;

	virtual TSubclassOf<UStateTreeSchema> GetSchema() const override
	{
		return UThistleStateTreeSchema::StaticClass();
	}
};
