
#include "FDebrisPool.h"
#include "BarrageDispatch.h"
#include "BarrageSpawnUtils.h"
#include "EPhysicsLayer.h"
#include "FBShapeParams.h"
#include "Skeletonize.h"

void FDebrisPool::Initialize(UBarrageDispatch* InDispatch, float ColliderHalfExtent, int32 PrewarmCount)
{
	check(InDispatch);
	CachedDispatch = InDispatch;
	CachedColliderHalfExtent = FMath::Max(ColliderHalfExtent, 1.f);

	if (PrewarmCount > 0)
	{
		Slots.Reserve(PrewarmCount);
		FreeList.Reserve(PrewarmCount);

		for (int32 i = 0; i < PrewarmCount; ++i)
		{
			const int32 Idx = AllocateNewSlot();
			if (Idx != INDEX_NONE)
			{
				FreeList.Add(Idx);
			}
		}

		UE_LOG(LogTemp, Log, TEXT("FDebrisPool: Pre-warmed %d bodies (HalfExtent=%.1f)"), PrewarmCount, CachedColliderHalfExtent);
	}
}

FSkeletonKey FDebrisPool::Acquire(const FVector& Position, const FQuat& Rotation)
{
	check(CachedDispatch);

	int32 SlotIndex;
	if (FreeList.Num() > 0)
	{
		SlotIndex = FreeList.Pop(EAllowShrinking::No);
	}
	else
	{
		SlotIndex = AllocateNewSlot();
		if (SlotIndex == INDEX_NONE)
		{
			return FSkeletonKey();
		}
	}

	FPoolSlot& Slot = Slots[SlotIndex];
	Slot.bActive = true;

	// Move body to position and activate — SYNCHRONOUS (not queued).
	// Must be immediate so constraint auto-detect sees correct positions.
	CachedDispatch->SetBodyPositionDirect(Slot.Primitive->KeyIntoBarrage, Position);
	CachedDispatch->SetBodyRotationDirect(Slot.Primitive->KeyIntoBarrage, Rotation);
	CachedDispatch->SetBodyObjectLayer(Slot.Primitive->KeyIntoBarrage, Layers::MOVING);

	return Slot.EntityKey;
}

void FDebrisPool::Release(FSkeletonKey EntityKey)
{
	check(CachedDispatch);

	const int32 SlotIndex = FindSlotByKey(EntityKey);
	if (SlotIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("FDebrisPool::Release - Key not found in pool!"));
		return;
	}

	FPoolSlot& Slot = Slots[SlotIndex];
	if (!Slot.bActive)
	{
		UE_LOG(LogTemp, Warning, TEXT("FDebrisPool::Release - Slot already released!"));
		return;
	}

	Slot.bActive = false;

	// Synchronously zero BOTH linear and angular velocity, then move to DEBRIS layer.
	// Must be synchronous (not queued) so the body is fully reset before re-Acquire.
	// Old code only zeroed linear velocity via queued SetVelocity — angular velocity
	// carried over to next Acquire, causing inconsistent impulse behavior.
	CachedDispatch->ResetBodyVelocities(Slot.Primitive->KeyIntoBarrage);
	CachedDispatch->SetBodyObjectLayer(Slot.Primitive->KeyIntoBarrage, Layers::DEBRIS);

	FreeList.Add(SlotIndex);
}

int32 FDebrisPool::AllocateNewSlot()
{
	check(CachedDispatch);

	// Generate unique key for this pooled body
	FSkeletonKey EntityKey = FBarrageSpawnUtils::GenerateUniqueKey();

	// Far below world — body will sleep immediately on DEBRIS layer
	const FVector PooledPosition(0.0, 0.0, -100000.0);

	const double HE = static_cast<double>(CachedColliderHalfExtent);
	FBBoxParams BoxParams = FBarrageBounder::GenerateBoxBounds(
		PooledPosition,
		HE * 2.0, HE * 2.0, HE * 2.0,
		FVector3d::ZeroVector,
		FMassByCategory::MostEnemies
	);

	// Create on DEBRIS layer (dormant, only collides with NON_MOVING)
	FBLet Body = CachedDispatch->CreatePrimitive(
		BoxParams,
		EntityKey,
		Layers::DEBRIS,
		false,  // not sensor
		true,   // force dynamic (fragments need to move)
		true,   // movable
		0.4f,   // friction
		0.2f,   // restitution
		0.1f    // linear damping
	);

	if (!FBarragePrimitive::IsNotNull(Body))
	{
		UE_LOG(LogTemp, Error, TEXT("FDebrisPool: Failed to create pooled body!"));
		return INDEX_NONE;
	}

	// Zero gravity by default — FragmentationSystem sets gravity per fragment
	FBarragePrimitive::SetGravityFactor(1.0f, Body);

	FPoolSlot Slot;
	Slot.EntityKey = EntityKey;
	Slot.Primitive = Body;
	Slot.bActive = false;

	const int32 NewIndex = Slots.Add(Slot);
	// NOTE: Don't add to FreeList here — caller decides.
	// Initialize() adds to FreeList for prewarm; Acquire() uses the index directly.
	return NewIndex;
}

int32 FDebrisPool::FindSlotByKey(FSkeletonKey Key) const
{
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].EntityKey == Key)
		{
			return i;
		}
	}
	return INDEX_NONE;
}
