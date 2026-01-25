#include "ThistleStateTreeCore.h"

#include "StateTreeConditionBase.h"
#include "ThistleBehavioralist.h"
#include "ThistleDispatch.h"
#include "Components/StateTreeComponentSchema.h"

EStateTreeRunStatus FStoreRelationship::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const IdentPtr V = UArtilleryDispatch::SelfPtr->GetOrAddIdent(InstanceData.SourceKey, InstanceData.Relationship);
	if (V)
	{
		V->SetCurrentValue(InstanceData.UpdateToRelatedKey);
		return EStateTreeRunStatus::Succeeded;
	}
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSetTagOfKey::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	//This seems unsafe.
	UArtilleryDispatch::SelfPtr->AddTagToEntity(InstanceData.KeyOf, InstanceData.Tag);
	return EStateTreeRunStatus::Succeeded;
}

EStateTreeRunStatus FRemoveTagFromKey::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	//This seems unsafe.
	UArtilleryDispatch::SelfPtr->RemoveTagFromEntity(InstanceData.KeyOf, InstanceData.Tag);
	return EStateTreeRunStatus::Succeeded;
}

EStateTreeRunStatus FStoreToAttribute::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AttrPtr V = UArtilleryDispatch::SelfPtr->GetAttrib(InstanceData.KeyOf, InstanceData.AttributeName);
	if (V)
	{
		V->SetCurrentValue(InstanceData.Value);
		return EStateTreeRunStatus::Succeeded;
	}
	return EStateTreeRunStatus::Running;
}

void UThistleStateTreeLease::BeginDestroy()
{
	IsReady = false;
	bIsRunning = false;
	Super::BeginDestroy();
}

void UThistleStateTreeLease::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	IsReady = false;
	bIsRunning = false;
	Super::EndPlay(EndPlayReason);
}

void UThistleStateTreeLease::OnUnregister()
{
	IsReady = false;
	bIsRunning = false;
	Super::OnUnregister();
}

#if WITH_GAMEPLAY_DEBUGGER
FString UThistleStateTreeLease::GetDebugInfoString() const
{
	if (this && GetOwner() && StateTreeRef.IsValid())
	{
		if (!StateTreeRef.IsValid())
		{
			return FString("No StateTree to run.");
		}
		return FConstStateTreeExecutionContextView(*GetOwner(), *StateTreeRef.GetStateTree(), InstanceData).Get().GetDebugInfoString();
	}
	return FString("No StateTree to run.");
}
#endif // WITH_GAMEPLAY_DEBUGGER

void UThistleStateTreeLease::InitializeComponent()
{
	Super::InitializeComponent();
}

bool UThistleStateTreeLease::CollectExternalData(
	const FStateTreeExecutionContext& Context,
	const UStateTree* StateTree,
	TArrayView<const FStateTreeExternalDataDesc> Descs,
	TArrayView<FStateTreeDataView> OutDataViews) const
{
	auto ErrantWays = const_cast<F_ArtilleryKeyInstanceData*>(&InstanceOwnerKey);
	bool First = UThistleStateTreeSchema::CollectExternalData(Context, ErrantWays, Descs, OutDataViews);
	return First && Super::CollectExternalData(Context, StateTree, Descs, OutDataViews);
}

void UThistleStateTreeLease::OnRegister()
{
	Super::OnRegister();
	AttemptRegister();
}

bool UThistleStateTreeLease::RegistrationImplementation()
{
	return GetWorld()->GetSubsystem<UThistleBehavioralist>() != nullptr;
}

void UThistleStateTreeLease::OnClusterMarkedAsPendingKill()
{
	IsReady = false;
	bIsRunning = false;
	Super::OnClusterMarkedAsPendingKill();
}

void UThistleStateTreeLease::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	IsReady = false;
	bIsRunning = false;
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

UThistleStateTreeLease::UThistleStateTreeLease(const FObjectInitializer& ObjectInitializer): Super(ObjectInitializer), CurrentRunStatus()
{
	bWantsInitializeComponent = true;
	bIsRunning = true;
	bIsPaused = false;
	bStartLogicAutomatically = true;
}

UThistleStateTreeLease::~UThistleStateTreeLease()
{
	IsReady = false;
}

void UThistleStateTreeLease::BeginPlay()
{
	Super::BeginPlay();

	BindDel = FOnCollectStateTreeExternalData::CreateUObject(this, &UThistleStateTreeLease::CollectExternalData);
	InstanceData = FStateTreeInstanceData();
	FStateTreeExecutionContext Context(*GetOwner(), *StateTreeRef.GetStateTree(), InstanceData);

	Context.Start();
	AttemptRegister();
}

FSkeletonKey UThistleStateTreeLease::GetMyKey() const
{
	IKeyedConstruct* KeyedConstruct = Cast<IKeyedConstruct>(GetOwner());
	return KeyedConstruct ? KeyedConstruct->GetMyKey() : FSkeletonKey();
}

UGameplayTasksComponent* UThistleStateTreeLease::GetGameplayTasksComponent(const UGameplayTask& Task) const
{
	UE_LOG(LogTemp, Error,
	       TEXT("UStateShrub runs in Artillery cadence and just tried to provide a gameplay tasks component from main thread. This could be real bad."));
	return Super::GetGameplayTasksComponent(Task);
}

bool UThistleStateTreeLease::SetContextRequirements(FStateTreeExecutionContext& Context, bool bLogErrors)
{
	Context.SetLinkedStateTreeOverrides(&LinkedStateTreeOverrides);
	InstanceOwnerKey = GetMyKey(); // failsafe.
	Context.SetCollectExternalDataCallback(BindDel);
	return UThistleStateTreeSchema::SetContextRequirements(GetMyKey(), Context) &&
		UStateTreeComponentSchema::SetContextRequirements(*this, Context);
}

void UThistleStateTreeLease::TickComponent(float DeltaTime, enum ELevelTick TickType,
                                           FActorComponentTickFunction* ThisTickFunction)
{
	if (!IsReady)
	{
		AttemptRegister();
	}
	else
	{
		SetComponentTickEnabled(false);
	}
}

void UThistleStateTreeLease::ArtilleryTick(uint64_t TicksSoFar)
{
	if (this && GetOwner() && IsReady)
	{
		if (MessagesToProcess.Num() > 0)
		{
			const int32 NumMessages = MessagesToProcess.Num();
			for (int32 Idx = 0; Idx < NumMessages; Idx++)
			{
				// create a copy of message in case MessagesToProcess is changed during loop
				const FAIMessage MessageCopy(MessagesToProcess[Idx]);
				for (int32 ObserverIndex = 0; ObserverIndex < MessageObservers.Num(); ObserverIndex++)
				{
					MessageObservers[ObserverIndex]->OnMessage(MessageCopy);
				}
			}
			MessagesToProcess.RemoveAt(0, NumMessages, EAllowShrinking::No);
		}
		InstanceOwnerKey = GetMyKey(); //freshen up, me hearties, yo ho.
		FStateTreeReference IncrementRefAsGuard = StateTreeRef; //Don't delete this. No, seriously. Don't you dare.
		FStateTreeExecutionContext Context(*GetOwner(), *IncrementRefAsGuard.GetStateTree(), InstanceData);

		if ((!bIsRunning || bIsPaused) || !StateTreeRef.IsValid())
		{
			return;
		}
		
		if (SetContextRequirements(Context))
		{
			const EStateTreeRunStatus PreviousRunStatus = Context.GetStateTreeRunStatus();
			if (PreviousRunStatus != EStateTreeRunStatus::Unset && Context.IsValid() //fine. FINE.
				&& Context.GetMutableInstanceData()->GetExecutionState()->TreeRunStatus != EStateTreeRunStatus::Unset
				 && bIsRunning && GetOwner() && GetWorld() && IsReady)
			{
				CurrentRunStatus = Context.Tick(1.0 / ArtilleryTickHertz);
			}
		}
	}
}

////////////////////////
