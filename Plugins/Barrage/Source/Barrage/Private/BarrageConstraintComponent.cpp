
#include "BarrageConstraintComponent.h"
#include "BarrageDispatch.h"
#include "FWorldSimOwner.h"
#include "BarrageConstraintSystem.h"
#include "KeyCarry.h"
#include "BarrageBodyOwner.h"

UBarrageConstraintComponent::UBarrageConstraintComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UBarrageConstraintComponent::BeginPlay()
{
	Super::BeginPlay();

	// Cache the dispatch
	if (UWorld* World = GetWorld())
	{
		CachedDispatch = World->GetSubsystem<UBarrageDispatch>();
	}

	// Enable tick only if auto-processing breaking
	if (bAutoProcessBreaking)
	{
		SetComponentTickEnabled(true);
	}
}

void UBarrageConstraintComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveAllConstraints();
	Super::EndPlay(EndPlayReason);
}

void UBarrageConstraintComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bAutoProcessBreaking)
	{
		ProcessBreaking();
	}
}

FBarrageConstraintSystem* UBarrageConstraintComponent::GetConstraintSystem() const
{
	if (!CachedDispatch.IsValid())
	{
		return nullptr;
	}

	UBarrageDispatch* Dispatch = CachedDispatch.Get();
	if (!Dispatch || !Dispatch->JoltGameSim)
	{
		return nullptr;
	}

	return &Dispatch->JoltGameSim->GetConstraints();
}

FBarrageKey UBarrageConstraintComponent::GetActorBodyKey(AActor* Actor) const
{
	if (!Actor || !CachedDispatch.IsValid())
	{
		return FBarrageKey();
	}

	// Try to get the body key from the actor's components
	// This assumes actors have a way to expose their Barrage key
	// For now, we'll use the primitive component approach

	// TODO: This needs integration with how your actors expose their FBarrageKey
	// For example, through an interface or specific component

	return FBarrageKey();
}

int32 UBarrageConstraintComponent::CreateAllConstraints()
{
	FBarrageConstraintSystem* System = GetConstraintSystem();
	if (!System)
	{
		return 0;
	}

	int32 Created = 0;
	for (int32 i = 0; i < Constraints.Num(); ++i)
	{
		if (!Constraints[i].bCreated)
		{
			if (CreateConstraint(Constraints[i]) >= 0)
			{
				Created++;
			}
		}
	}

	return Created;
}

void UBarrageConstraintComponent::RemoveAllConstraints()
{
	FBarrageConstraintSystem* System = GetConstraintSystem();
	if (!System)
	{
		return;
	}

	for (FBarrageConstraintDefinition& Def : Constraints)
	{
		if (Def.bCreated && Def.RuntimeKey.IsValid())
		{
			System->Remove(Def.RuntimeKey);
			Def.bCreated = false;
			Def.RuntimeKey = FBarrageConstraintKey();
		}
	}
}

int32 UBarrageConstraintComponent::CreateConstraint(const FBarrageConstraintDefinition& Definition)
{
	FBarrageConstraintSystem* System = GetConstraintSystem();
	if (!System)
	{
		return -1;
	}

	// Get body keys
	FBarrageKey Body1 = OwnerBodyKey;
	FBarrageKey Body2;
	if (Definition.TargetActor.IsValid())
	{
		Body2 = GetActorBodyKey(Definition.TargetActor.Get());
	}

	FBarrageConstraintKey Key;

	// Create based on type
	switch (Definition.Type)
	{
	case EBConstraintType::Fixed:
		{
			FBFixedConstraintParams Params;
			Params.Body1 = Body1;
			Params.Body2 = Body2;
			Params.Space = EBConstraintSpace::WorldSpace;
			Params.bAutoDetectAnchor = false;
			Params.AnchorPoint1 = GetOwner()->GetActorLocation() + Definition.LocalAnchorOffset;
			if (Definition.TargetActor.IsValid())
			{
				Params.AnchorPoint2 = Definition.TargetActor->GetActorLocation() + Definition.TargetAnchorOffset;
			}
			Params.BreakForce = Definition.BreakForce;
			Params.BreakTorque = Definition.BreakTorque;
			Key = System->CreateFixed(Params);
		}
		break;

	case EBConstraintType::Point:
		{
			FBPointConstraintParams Params;
			Params.Body1 = Body1;
			Params.Body2 = Body2;
			Params.Space = EBConstraintSpace::WorldSpace;
			Params.AnchorPoint1 = GetOwner()->GetActorLocation() + Definition.LocalAnchorOffset;
			if (Definition.TargetActor.IsValid())
			{
				Params.AnchorPoint2 = Definition.TargetActor->GetActorLocation() + Definition.TargetAnchorOffset;
			}
			Params.BreakForce = Definition.BreakForce;
			Params.BreakTorque = Definition.BreakTorque;
			Key = System->CreatePoint(Params);
		}
		break;

	case EBConstraintType::Hinge:
		{
			FBHingeConstraintParams Params;
			Params.Body1 = Body1;
			Params.Body2 = Body2;
			Params.Space = EBConstraintSpace::WorldSpace;
			Params.AnchorPoint1 = GetOwner()->GetActorLocation() + Definition.LocalAnchorOffset;
			if (Definition.TargetActor.IsValid())
			{
				Params.AnchorPoint2 = Definition.TargetActor->GetActorLocation() + Definition.TargetAnchorOffset;
			}
			Params.HingeAxis = Definition.HingeAxis;
			Params.bHasLimits = Definition.bHingeLimits;
			Params.MinAngle = FMath::DegreesToRadians(Definition.HingeMinAngle);
			Params.MaxAngle = FMath::DegreesToRadians(Definition.HingeMaxAngle);
			Params.BreakForce = Definition.BreakForce;
			Params.BreakTorque = Definition.BreakTorque;
			Key = System->CreateHinge(Params);
		}
		break;

	case EBConstraintType::Distance:
		{
			FBDistanceConstraintParams Params;
			Params.Body1 = Body1;
			Params.Body2 = Body2;
			Params.Space = EBConstraintSpace::WorldSpace;
			Params.AnchorPoint1 = GetOwner()->GetActorLocation() + Definition.LocalAnchorOffset;
			if (Definition.TargetActor.IsValid())
			{
				Params.AnchorPoint2 = Definition.TargetActor->GetActorLocation() + Definition.TargetAnchorOffset;
			}
			Params.MinDistance = Definition.MinDistance;
			Params.MaxDistance = Definition.MaxDistance;
			Params.SpringFrequency = Definition.SpringFrequency;
			Params.SpringDamping = Definition.SpringDamping;
			Params.BreakForce = Definition.BreakForce;
			Params.BreakTorque = Definition.BreakTorque;
			Key = System->CreateDistance(Params);
		}
		break;

	case EBConstraintType::Cone:
		{
			FBConeConstraintParams Params;
			Params.Body1 = Body1;
			Params.Body2 = Body2;
			Params.Space = EBConstraintSpace::WorldSpace;
			Params.AnchorPoint1 = GetOwner()->GetActorLocation() + Definition.LocalAnchorOffset;
			if (Definition.TargetActor.IsValid())
			{
				Params.AnchorPoint2 = Definition.TargetActor->GetActorLocation() + Definition.TargetAnchorOffset;
			}
			Params.BreakForce = Definition.BreakForce;
			Params.BreakTorque = Definition.BreakTorque;
			Key = System->CreateCone(Params);
		}
		break;

	default:
		return -1;
	}

	if (!Key.IsValid())
	{
		return -1;
	}

	// Add to our list
	int32 Index = Constraints.Add(Definition);
	Constraints[Index].RuntimeKey = Key;
	Constraints[Index].bCreated = true;

	return Index;
}

bool UBarrageConstraintComponent::RemoveConstraint(int32 Index)
{
	if (!Constraints.IsValidIndex(Index))
	{
		return false;
	}

	FBarrageConstraintSystem* System = GetConstraintSystem();
	if (!System)
	{
		return false;
	}

	if (Constraints[Index].bCreated && Constraints[Index].RuntimeKey.IsValid())
	{
		System->Remove(Constraints[Index].RuntimeKey);
	}

	Constraints.RemoveAt(Index);
	return true;
}

bool UBarrageConstraintComponent::IsConstraintActive(int32 Index) const
{
	if (!Constraints.IsValidIndex(Index))
	{
		return false;
	}

	const FBarrageConstraintSystem* System = GetConstraintSystem();
	if (!System)
	{
		return false;
	}

	return Constraints[Index].bCreated &&
		   Constraints[Index].RuntimeKey.IsValid() &&
		   System->IsValid(Constraints[Index].RuntimeKey);
}

bool UBarrageConstraintComponent::GetConstraintForce(int32 Index, float& OutForce) const
{
	if (!Constraints.IsValidIndex(Index) || !Constraints[Index].bCreated)
	{
		return false;
	}

	const FBarrageConstraintSystem* System = GetConstraintSystem();
	if (!System)
	{
		return false;
	}

	FBConstraintForces Forces;
	if (System->GetForces(Constraints[Index].RuntimeKey, Forces))
	{
		OutForce = Forces.GetForceMagnitude();
		return true;
	}

	return false;
}

bool UBarrageConstraintComponent::GetConstraintStressRatio(int32 Index, float& OutRatio) const
{
	if (!Constraints.IsValidIndex(Index) || !Constraints[Index].bCreated)
	{
		return false;
	}

	const FBarrageConstraintDefinition& Def = Constraints[Index];
	if (Def.BreakForce <= 0.0f && Def.BreakTorque <= 0.0f)
	{
		// No break threshold set
		return false;
	}

	const FBarrageConstraintSystem* System = GetConstraintSystem();
	if (!System)
	{
		return false;
	}

	FBConstraintForces Forces;
	if (!System->GetForces(Def.RuntimeKey, Forces))
	{
		return false;
	}

	float ForceRatio = Def.BreakForce > 0.0f ? Forces.GetForceMagnitude() / Def.BreakForce : 0.0f;
	float TorqueRatio = Def.BreakTorque > 0.0f ? Forces.GetTorqueMagnitude() / Def.BreakTorque : 0.0f;

	OutRatio = FMath::Max(ForceRatio, TorqueRatio);
	return true;
}

bool UBarrageConstraintComponent::BreakConstraint(int32 Index)
{
	if (!Constraints.IsValidIndex(Index) || !Constraints[Index].bCreated)
	{
		return false;
	}

	FBarrageConstraintSystem* System = GetConstraintSystem();
	if (!System)
	{
		return false;
	}

	if (System->Remove(Constraints[Index].RuntimeKey))
	{
		Constraints[Index].bCreated = false;
		Constraints[Index].RuntimeKey = FBarrageConstraintKey();
		OnConstraintBroken.Broadcast(Index);
		return true;
	}

	return false;
}

int32 UBarrageConstraintComponent::GetActiveConstraintCount() const
{
	int32 Count = 0;
	for (const FBarrageConstraintDefinition& Def : Constraints)
	{
		if (Def.bCreated && Def.RuntimeKey.IsValid())
		{
			Count++;
		}
	}
	return Count;
}

void UBarrageConstraintComponent::ProcessBreaking()
{
	FBarrageConstraintSystem* System = GetConstraintSystem();
	if (!System)
	{
		return;
	}

	for (int32 i = Constraints.Num() - 1; i >= 0; --i)
	{
		FBarrageConstraintDefinition& Def = Constraints[i];
		if (Def.bCreated && Def.RuntimeKey.IsValid())
		{
			if (System->ShouldBreak(Def.RuntimeKey))
			{
				System->Remove(Def.RuntimeKey);
				Def.bCreated = false;
				Def.RuntimeKey = FBarrageConstraintKey();
				OnConstraintBroken.Broadcast(i);
			}
		}
	}
}
