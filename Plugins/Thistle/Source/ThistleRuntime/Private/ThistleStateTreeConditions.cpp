#include "ThistleStateTreeConditions.h"

#include "ArtilleryBPLibs.h"
#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ThistleStateTreeConditions)

#if WITH_EDITOR
#define LOCTEXT_NAMESPACE "StateTreeEditor"
#endif// WITH_EDITOR

//----------------------------------------------------------------------//
//  GameplayTagMatchCondition
//----------------------------------------------------------------------//

bool FArtilleryTagMatchCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	bool found = false;
	FConservedTags Container = UArtilleryLibrary::InternalTagsByKey(InstanceData.KeyOf, found); //with the newer conserved tags, we COULD save this off..
	//TODO: add "has tag" support, not just has exact tag support.
	//return (bExactMatch ?  Container->Find(InstanceData.Tag) : Container->HasTag(InstanceData.Tag)) ^ bInvert;
	return found && Container->Find(InstanceData.Tag) ^ bInvert;
}

bool FArtilleryAttributeValueCondition::Test(float Value, float Target) const
{
	return TreeOperandTest(Value, Target, Operation);
}

bool FArtilleryAttributeValueCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	bool Found = false;
	float Value = UArtilleryLibrary::implK2_GetAttrib(InstanceData.KeyOf, InstanceData.AttributeName, Found);
	return Found && Test(Value, TestValue);
}

bool FArtilleryAttributeCompareCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	bool Found = false;
	bool TargetFound = false;
	float Value = UArtilleryLibrary::implK2_GetAttrib(InstanceData.KeyOf, InstanceData.AttributeName, Found);
	float TestAttrib = UArtilleryLibrary::implK2_GetAttrib(InstanceData.KeyOf, InstanceData.AttributeName, Found);
	if (Found)
	{
		if (TargetFound)
		{
			return Test(Value, TestAttrib);
		}
		if (bFallbackToTestValue)
		{
			return Test(Value, TestValue);
		}
	}
	return false;
}


bool FArtilleryCompareRelatedCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	bool SourceKey_AttributeFound = false;
	bool RelatedKey_AttributeFound = false;
	bool RelatedKey_FoundAtAll = false;


	auto Identity = UArtilleryLibrary::K2_GetIdentity(InstanceData.KeyOf, Relationship, RelatedKey_FoundAtAll);
	if (RelatedKey_AttributeFound)
	{
		auto TestAttribValue = UArtilleryLibrary::implK2_GetAttrib(Identity, InstanceData.AttributeName, RelatedKey_AttributeFound);
		if (RelatedKey_AttributeFound)
		{
			auto SourceValue = UArtilleryLibrary::implK2_GetAttrib(InstanceData.KeyOf, InstanceData.AttributeName, SourceKey_AttributeFound);
			if (bCompareWithTargetKeyAttribute && SourceKey_AttributeFound)
			{
				return Test(TestAttribValue, SourceValue);
			}
			return Test(TestAttribValue, TestValue);
		}
	}
	return false;
}

bool FArtilleryCompareKeys::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (	!InstanceData.SourceKey.IsValid()
		||	!InstanceData.TargetKey.IsValid()
		||	InstanceData.SourceKey != InstanceData.TargetKey)
	{
		return false;
	}
	return true;
}

bool FCheckLoStoPoI::TestCondition(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	bool Shucked = false;
	auto Source = InstanceData.Source.ShuckPoi(Shucked);
	if (!Shucked) { return false; }
	auto Target = InstanceData.Target.ShuckPoi(Shucked);
	// ReSharper disable once CppRedundantControlFlowJump
	if (!Shucked) { return false; }
	auto ToFrom = (Target - Source);
	auto Length = ToFrom.Length() + 1;
	if ((UArtilleryLibrary::GetTotalsTickCount() % InstanceData.TicksBetweenCastRefresh) == 0 &&
		UBarrageDispatch::SelfPtr)
	{
		const JPH::DefaultBroadPhaseLayerFilter default_broadphase_layer_filter = UBarrageDispatch::SelfPtr->JoltGameSim
			->physics_system->GetDefaultBroadPhaseLayerFilter(Layers::CAST_QUERY);
		const JPH::DefaultObjectLayerFilter default_object_layer_filter = UBarrageDispatch::SelfPtr->JoltGameSim->
			physics_system->GetDefaultLayerFilter(Layers::CAST_QUERY);

		if (InstanceData.SourceBodyKey_SetOrRegret.IsValid()
			&& UArtilleryDispatch::SelfPtr->IsLiveKey(InstanceData.SourceBodyKey_SetOrRegret) != DEAD)
		{
			JPH::IgnoreSingleBodyFilter StopHittingYourself = UBarrageDispatch::SelfPtr->GetFilterToIgnoreSingleBody(
				UBarrageDispatch::SelfPtr->GetShapeRef(InstanceData.SourceBodyKey_SetOrRegret)->KeyIntoBarrage);
			UBarrageDispatch::SelfPtr->SphereCast(InstanceData.Radius, Length, Source, ToFrom.GetSafeNormal(),
												  InstanceData.HitResultCache, default_broadphase_layer_filter,
												  default_object_layer_filter, StopHittingYourself);
			
			InstanceData.Outcome = *(InstanceData.HitResultCache);
			bool KeyExist = InstanceData.Target.PointOfInterestKey.IsValid();
			bool HitTarget = false;
			bool HitClose = false;
			if (KeyExist)
			{
				if (InstanceData.Outcome.MyItem != JPH::BodyID::cInvalidBodyID)
				{
					FBarrageKey HitBarrageKey = UBarrageDispatch::SelfPtr->GenerateBarrageKeyFromBodyId(
					static_cast<uint32>(InstanceData.Outcome.MyItem));
					FBLet HitObjectFiblet = UBarrageDispatch::SelfPtr->GetShapeRef(HitBarrageKey);
					if (HitObjectFiblet && HitObjectFiblet->KeyOutOfBarrage == InstanceData.Target.PointOfInterestKey)
					{
						HitTarget = true;
					}
				}
			}
			if (Target.Equals(InstanceData.Outcome.ImpactPoint, DistanceFromTargetToTolerate))
			{
				HitClose = true;
			}
			return HitTarget || HitClose;
		}
	}
	else
	{
		return false;
	}
	return false;
}

#if WITH_EDITOR
#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR

