
#include "PlayerKine.h"
#include "BarrageDispatch.h"

TOptional<FTransform> BarrageKine::CopyOfTransformlike_Impl()
{
	UBarrageDispatch* Dispatch = BarrageDispatch.Get();
	if (!Dispatch)
	{
		return TOptional<FTransform>();
	}

	FBLet Body = Dispatch->GetShapeRef(MyKey);
	if (!FBarragePrimitive::IsNotNull(Body))
	{
		return TOptional<FTransform>();
	}

	FVector3f Position = FBarragePrimitive::GetPosition(Body);
	FQuat4f Rotation = FBarragePrimitive::OptimisticGetAbsoluteRotation(Body);

	FTransform Result;
	Result.SetLocation(FVector(Position.X, Position.Y, Position.Z));
	Result.SetRotation(FQuat(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W));
	Result.SetScale3D(FVector::OneVector);

	return Result;
}
