
#pragma once

#include "CoreMinimal.h"
#include "Kines.h"
#include "FBarragePrimitive.h"

class UBarrageDispatch;

/**
 * BarrageKine - A Kine for physics bodies without an Actor.
 * Used for rendering large numbers of physics-driven entities (bullets, debris, etc.)
 * via NDC/Niagara without Actor overhead.
 *
 * NOTE: Set methods are no-ops since physics controls the transform.
 * Only reading (CopyOfTransformlike) is functional.
 */
class BARRAGE_API BarrageKine : public Kine
{
	TWeakObjectPtr<UBarrageDispatch> BarrageDispatch;

public:
	explicit BarrageKine(const TWeakObjectPtr<UBarrageDispatch>& InBarrageDispatch, const FSkeletonKey& BodyKey)
		: BarrageDispatch(InBarrageDispatch)
	{
		MyKey = BodyKey;
	}

	// Physics controls transforms - these are intentionally no-ops
	virtual void SetTransformlike(FTransform Input) override { }
	virtual void SetLocation(FVector3d Location) override { }
	virtual void SetRotation(FQuat4d Rotation) override { }
	virtual void SetLocationAndRotation(FVector3d Loc, FQuat4d Rot) override { }
	virtual void SetLocationAndRotationWithScope(FVector3d Loc, FQuat4d Rot) override { }

protected:
	virtual TOptional<FTransform> CopyOfTransformlike_Impl() override;
};

class PlayerKine : public ActorKine
{
	//static inline FActorComponentTickFunction FALSEHOOD_MALEVOLENCE_TRICKERY = FActorComponentTickFunction();
public:
	TWeakObjectPtr<AActor> MySelf;
	
	explicit PlayerKine(const TWeakObjectPtr<AActor>& MySelf, const ActorKey& Target) : ActorKine(MySelf, Target), MySelf(MySelf)
	{
		MyKey = Target;
	}

	virtual void SetLocationAndRotation(FVector3d Loc, FQuat4d Rot) override
	{
		TObjectPtr<AActor> Pin = MySelf.Get();
		if(Pin && !Loc.ContainsNaN())
		{
			Pin->SetActorLocationAndRotation(Loc, Rot);
		}
	}

	virtual void SetLocation(FVector3d Location) override
	{
		TObjectPtr<AActor> Pin = MySelf.Get();
		if(Pin && !Location.ContainsNaN())
		{
			Pin->SetActorLocation(Location, false, nullptr, ETeleportType::None);
		}
	}

	virtual void SetRotation(FQuat4d Rotation) override
	{
		TObjectPtr<AActor> Pin = MySelf.Get();
		if(Pin && !Rotation.ContainsNaN())
		{
			Pin->SetActorRotation(Rotation, ETeleportType::None);
		}
	}
};
