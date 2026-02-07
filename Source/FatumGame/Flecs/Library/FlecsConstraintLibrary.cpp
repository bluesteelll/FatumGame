
#include "FlecsConstraintLibrary.h"
#include "FlecsLibraryHelpers.h"
#include "BarrageConstraintSystem.h"
#include "FlecsBarrageComponents.h"
#include "FlecsGameTags.h"

// ═══════════════════════════════════════════════════════════════
// CONSTRAINTS
// ═══════════════════════════════════════════════════════════════

int64 UFlecsConstraintLibrary::CreateFixedConstraint(
	UObject* WorldContextObject,
	FSkeletonKey Entity1Key,
	FSkeletonKey Entity2Key,
	float BreakForce,
	float BreakTorque)
{
	UBarrageDispatch* Barrage = FlecsLibrary::GetBarrageDispatch(WorldContextObject);
	UFlecsArtillerySubsystem* FlecsSubsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Barrage || !FlecsSubsystem) return 0;

	FBarrageKey Body1 = Barrage->GetBarrageKeyFromSkeletonKey(Entity1Key);
	FBarrageKey Body2 = Barrage->GetBarrageKeyFromSkeletonKey(Entity2Key);

	if (Body1.KeyIntoBarrage == 0 || Body2.KeyIntoBarrage == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("CreateFixedConstraint: Invalid body keys! Entity1=%llu Entity2=%llu"),
			static_cast<uint64>(Entity1Key), static_cast<uint64>(Entity2Key));
		return 0;
	}

	FBarrageConstraintKey ConstraintKey = Barrage->CreateFixedConstraint(Body1, Body2, BreakForce, BreakTorque);
	if (!ConstraintKey.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("CreateFixedConstraint: Failed to create constraint!"));
		return 0;
	}

	int64 KeyValue = ConstraintKey.Key;
	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Entity1Key, Entity2Key, KeyValue, BreakForce, BreakTorque]()
	{
		flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		flecs::entity E1 = FlecsSubsystem->GetEntityForBarrageKey(Entity1Key);
		if (E1.is_valid() && E1.is_alive())
		{
			if (!E1.has<FFlecsConstraintData>())
			{
				E1.set<FFlecsConstraintData>({});
			}
			FFlecsConstraintData* Data = E1.try_get_mut<FFlecsConstraintData>();
			if (Data)
			{
				Data->AddConstraint(KeyValue, Entity2Key, BreakForce, BreakTorque);
			}
			E1.add<FTagConstrained>();
		}

		flecs::entity E2 = FlecsSubsystem->GetEntityForBarrageKey(Entity2Key);
		if (E2.is_valid() && E2.is_alive())
		{
			if (!E2.has<FFlecsConstraintData>())
			{
				E2.set<FFlecsConstraintData>({});
			}
			FFlecsConstraintData* Data = E2.try_get_mut<FFlecsConstraintData>();
			if (Data)
			{
				Data->AddConstraint(KeyValue, Entity1Key, BreakForce, BreakTorque);
			}
			E2.add<FTagConstrained>();
		}
	});

	UE_LOG(LogTemp, Log, TEXT("CreateFixedConstraint: Created constraint %lld between %llu and %llu"),
		KeyValue, static_cast<uint64>(Entity1Key), static_cast<uint64>(Entity2Key));

	return KeyValue;
}

int64 UFlecsConstraintLibrary::CreateHingeConstraint(
	UObject* WorldContextObject,
	FSkeletonKey Entity1Key,
	FSkeletonKey Entity2Key,
	FVector WorldAnchor,
	FVector HingeAxis,
	float BreakForce)
{
	UBarrageDispatch* Barrage = FlecsLibrary::GetBarrageDispatch(WorldContextObject);
	UFlecsArtillerySubsystem* FlecsSubsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Barrage || !FlecsSubsystem) return 0;

	FBarrageKey Body1 = Barrage->GetBarrageKeyFromSkeletonKey(Entity1Key);
	FBarrageKey Body2 = Barrage->GetBarrageKeyFromSkeletonKey(Entity2Key);

	if (Body1.KeyIntoBarrage == 0 || Body2.KeyIntoBarrage == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("CreateHingeConstraint: Invalid body keys!"));
		return 0;
	}

	FBarrageConstraintKey ConstraintKey = Barrage->CreateHingeConstraint(Body1, Body2, WorldAnchor, HingeAxis, BreakForce);
	if (!ConstraintKey.IsValid())
	{
		return 0;
	}

	int64 KeyValue = ConstraintKey.Key;
	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Entity1Key, Entity2Key, KeyValue, BreakForce]()
	{
		flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		auto UpdateEntity = [&](flecs::entity E, FSkeletonKey OtherKey)
		{
			if (E.is_valid() && E.is_alive())
			{
				if (!E.has<FFlecsConstraintData>())
				{
					E.set<FFlecsConstraintData>({});
				}
				FFlecsConstraintData* Data = E.try_get_mut<FFlecsConstraintData>();
				if (Data)
				{
					Data->AddConstraint(KeyValue, OtherKey, BreakForce, 0.f);
				}
				E.add<FTagConstrained>();
			}
		};

		UpdateEntity(FlecsSubsystem->GetEntityForBarrageKey(Entity1Key), Entity2Key);
		UpdateEntity(FlecsSubsystem->GetEntityForBarrageKey(Entity2Key), Entity1Key);
	});

	return KeyValue;
}

int64 UFlecsConstraintLibrary::CreateDistanceConstraint(
	UObject* WorldContextObject,
	FSkeletonKey Entity1Key,
	FSkeletonKey Entity2Key,
	float MinDistance,
	float MaxDistance,
	float BreakForce,
	float SpringFrequency,
	float SpringDamping,
	bool bLockRotation)
{
	UBarrageDispatch* Barrage = FlecsLibrary::GetBarrageDispatch(WorldContextObject);
	UFlecsArtillerySubsystem* FlecsSubsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Barrage || !FlecsSubsystem) return 0;

	FBarrageKey Body1 = Barrage->GetBarrageKeyFromSkeletonKey(Entity1Key);
	FBarrageKey Body2 = Barrage->GetBarrageKeyFromSkeletonKey(Entity2Key);

	if (Body1.KeyIntoBarrage == 0 || Body2.KeyIntoBarrage == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("CreateDistanceConstraint: Invalid body keys!"));
		return 0;
	}

	UE_LOG(LogTemp, Warning, TEXT("ConstraintLib CreateDistance: MinDist=%.1f, MaxDist=%.1f, SpringFreq=%.2f, SpringDamp=%.2f, LockRot=%d"),
		MinDistance, MaxDistance, SpringFrequency, SpringDamping, bLockRotation ? 1 : 0);

	FBarrageConstraintKey ConstraintKey = Barrage->CreateDistanceConstraint(Body1, Body2, MinDistance, MaxDistance, BreakForce, SpringFrequency, SpringDamping, bLockRotation);
	if (!ConstraintKey.IsValid())
	{
		return 0;
	}

	int64 KeyValue = ConstraintKey.Key;
	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Entity1Key, Entity2Key, KeyValue, BreakForce]()
	{
		flecs::world* FlecsWorld = FlecsSubsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		auto UpdateEntity = [&](flecs::entity E, FSkeletonKey OtherKey)
		{
			if (E.is_valid() && E.is_alive())
			{
				if (!E.has<FFlecsConstraintData>())
				{
					E.set<FFlecsConstraintData>({});
				}
				FFlecsConstraintData* Data = E.try_get_mut<FFlecsConstraintData>();
				if (Data)
				{
					Data->AddConstraint(KeyValue, OtherKey, BreakForce, 0.f);
				}
				E.add<FTagConstrained>();
			}
		};

		UpdateEntity(FlecsSubsystem->GetEntityForBarrageKey(Entity1Key), Entity2Key);
		UpdateEntity(FlecsSubsystem->GetEntityForBarrageKey(Entity2Key), Entity1Key);
	});

	return KeyValue;
}

int64 UFlecsConstraintLibrary::CreatePointConstraint(
	UObject* WorldContextObject,
	FSkeletonKey Entity1Key,
	FSkeletonKey Entity2Key,
	float BreakForce,
	float BreakTorque)
{
	UBarrageDispatch* Barrage = FlecsLibrary::GetBarrageDispatch(WorldContextObject);
	UFlecsArtillerySubsystem* FlecsSubsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Barrage || !FlecsSubsystem) return 0;

	FBarrageKey Body1 = Barrage->GetBarrageKeyFromSkeletonKey(Entity1Key);
	FBarrageKey Body2 = Barrage->GetBarrageKeyFromSkeletonKey(Entity2Key);

	if (Body1.KeyIntoBarrage == 0 || Body2.KeyIntoBarrage == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("CreatePointConstraint: Invalid body keys!"));
		return 0;
	}

	FBarrageConstraintSystem* ConstraintSystem = Barrage->GetConstraintSystem();
	if (!ConstraintSystem) return 0;

	FBPointConstraintParams Params;
	Params.Body1 = Body1;
	Params.Body2 = Body2;
	Params.Space = EBConstraintSpace::WorldSpace;
	Params.bAutoDetectAnchor = true;
	Params.BreakForce = BreakForce;
	Params.BreakTorque = BreakTorque;

	FBarrageConstraintKey ConstraintKey = ConstraintSystem->CreatePoint(Params);
	if (!ConstraintKey.IsValid())
	{
		return 0;
	}

	int64 KeyValue = ConstraintKey.Key;
	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, Entity1Key, Entity2Key, KeyValue, BreakForce, BreakTorque]()
	{
		auto UpdateEntity = [&](flecs::entity E, FSkeletonKey OtherKey)
		{
			if (E.is_valid() && E.is_alive())
			{
				if (!E.has<FFlecsConstraintData>())
				{
					E.set<FFlecsConstraintData>({});
				}
				FFlecsConstraintData* Data = E.try_get_mut<FFlecsConstraintData>();
				if (Data)
				{
					Data->AddConstraint(KeyValue, OtherKey, BreakForce, BreakTorque);
				}
				E.add<FTagConstrained>();
			}
		};

		UpdateEntity(FlecsSubsystem->GetEntityForBarrageKey(Entity1Key), Entity2Key);
		UpdateEntity(FlecsSubsystem->GetEntityForBarrageKey(Entity2Key), Entity1Key);
	});

	return KeyValue;
}

bool UFlecsConstraintLibrary::RemoveConstraint(UObject* WorldContextObject, int64 ConstraintKey)
{
	UBarrageDispatch* Barrage = FlecsLibrary::GetBarrageDispatch(WorldContextObject);
	if (!Barrage || ConstraintKey == 0) return false;

	FBarrageConstraintKey Key;
	Key.Key = ConstraintKey;

	return Barrage->RemoveConstraint(Key);
}

int32 UFlecsConstraintLibrary::RemoveAllConstraintsFromEntity(UObject* WorldContextObject, FSkeletonKey EntityKey)
{
	UBarrageDispatch* Barrage = FlecsLibrary::GetBarrageDispatch(WorldContextObject);
	UFlecsArtillerySubsystem* FlecsSubsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Barrage || !FlecsSubsystem || !EntityKey.IsValid()) return 0;

	FBarrageKey BodyKey = Barrage->GetBarrageKeyFromSkeletonKey(EntityKey);
	if (BodyKey.KeyIntoBarrage == 0) return 0;

	FBarrageConstraintSystem* ConstraintSystem = Barrage->GetConstraintSystem();
	if (!ConstraintSystem) return 0;

	int32 RemovedCount = ConstraintSystem->RemoveAllForBody(BodyKey);

	FlecsSubsystem->EnqueueCommand([FlecsSubsystem, EntityKey]()
	{
		flecs::entity Entity = FlecsSubsystem->GetEntityForBarrageKey(EntityKey);
		if (Entity.is_valid() && Entity.is_alive())
		{
			Entity.remove<FFlecsConstraintData>();
			Entity.remove<FTagConstrained>();
		}
	});

	return RemovedCount;
}

bool UFlecsConstraintLibrary::IsConstraintActive(UObject* WorldContextObject, int64 ConstraintKey)
{
	UBarrageDispatch* Barrage = FlecsLibrary::GetBarrageDispatch(WorldContextObject);
	if (!Barrage || ConstraintKey == 0) return false;

	FBarrageConstraintSystem* ConstraintSystem = Barrage->GetConstraintSystem();
	if (!ConstraintSystem) return false;

	FBarrageConstraintKey Key;
	Key.Key = ConstraintKey;

	return ConstraintSystem->IsValid(Key);
}

bool UFlecsConstraintLibrary::GetConstraintStressRatio(UObject* WorldContextObject, int64 ConstraintKey, float& OutStressRatio)
{
	OutStressRatio = 0.f;

	UBarrageDispatch* Barrage = FlecsLibrary::GetBarrageDispatch(WorldContextObject);
	if (!Barrage || ConstraintKey == 0) return false;

	FBarrageConstraintSystem* ConstraintSystem = Barrage->GetConstraintSystem();
	if (!ConstraintSystem) return false;

	FBarrageConstraintKey Key;
	Key.Key = ConstraintKey;

	FBConstraintForces Forces;
	if (!ConstraintSystem->GetForces(Key, Forces))
	{
		return false;
	}

	OutStressRatio = Forces.GetForceMagnitude();
	return true;
}
