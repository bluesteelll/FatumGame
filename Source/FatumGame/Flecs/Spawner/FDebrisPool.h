// Debris body pool — reuses Barrage physics bodies to avoid constant Jolt allocate/deallocate.
// Uniform box collider per slot. Visual mesh varies via ISM (separate from physics).
// SIM THREAD ONLY for Acquire/Release. Initialize from game thread via CreatePrimitive.

#pragma once

#include "CoreMinimal.h"
#include "SkeletonTypes.h"
#include "FBarrageKey.h"
#include "FBarragePrimitive.h"

class UBarrageDispatch;

class FDebrisPool
{
public:
	/**
	 * Initialize the pool. Creates PrewarmCount bodies on DEBRIS layer.
	 * Call from game thread (BeginPlay) or sim thread (via EnqueueCommand).
	 *
	 * @param InDispatch Barrage physics dispatch (must outlive pool).
	 * @param ColliderHalfExtent Half-extent of the uniform box collider (cm).
	 * @param PrewarmCount Number of bodies to pre-allocate (0 = grow on demand only).
	 */
	void Initialize(UBarrageDispatch* InDispatch, float ColliderHalfExtent = 25.f, int32 PrewarmCount = 0);

	/**
	 * Acquire a body from the pool. Grows if empty.
	 * SIM THREAD ONLY — called from FragmentationSystem.
	 *
	 * @param Position World position to place the body.
	 * @param Rotation World rotation for the body.
	 * @return FSkeletonKey of the acquired body (for Flecs binding), or invalid if failed.
	 */
	FSkeletonKey Acquire(const FVector& Position, const FQuat& Rotation);

	/**
	 * Release a body back to the pool.
	 * SIM THREAD ONLY — called from DeadEntityCleanupSystem.
	 *
	 * @param EntityKey The FSkeletonKey returned by Acquire.
	 */
	void Release(FSkeletonKey EntityKey);

	/** How many total slots exist (active + free). */
	int32 GetPoolSize() const { return Slots.Num(); }

	/** How many slots are currently in use. */
	int32 GetActiveCount() const { return Slots.Num() - FreeList.Num(); }

	/** How many slots are available for immediate use. */
	int32 GetFreeCount() const { return FreeList.Num(); }

	/** Is the pool initialized? */
	bool IsInitialized() const { return CachedDispatch != nullptr; }

private:
	struct FPoolSlot
	{
		FSkeletonKey EntityKey;
		FBLet Primitive;     // Cached FBarragePrimitive pointer
		bool bActive = false;
	};

	/** Allocate a new Jolt body and add it to the pool. Returns slot index. */
	int32 AllocateNewSlot();

	/** Find slot by EntityKey. Returns INDEX_NONE if not found. */
	int32 FindSlotByKey(FSkeletonKey Key) const;

	TArray<FPoolSlot> Slots;
	TArray<int32> FreeList;

	UBarrageDispatch* CachedDispatch = nullptr;
	float CachedColliderHalfExtent = 25.f;
};
