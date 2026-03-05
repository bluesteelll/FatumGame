
#include "BarrageSpawnUtils.h"
#include <atomic>
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FBShapeParams.h"
#include "Skeletonize.h"
#include "FlecsRenderManager.h"
#include "Engine/StaticMesh.h"

// Global entity counter - used by ALL spawners to ensure unique keys
static std::atomic<uint32> GGlobalEntityCounter{0};

FSkeletonKey FBarrageSpawnUtils::GenerateUniqueKey(uint64 KeyType)
{
	const uint32 Id = GGlobalEntityCounter.fetch_add(1) + 1;
	return FSkeletonKey(FORGE_SKELETON_KEY(Id, KeyType));
}

FVector FBarrageSpawnUtils::CalculateColliderSize(UStaticMesh* Mesh, FVector Scale)
{
	if (!Mesh)
	{
		return FVector(100, 100, 100);
	}

	FBoxSphereBounds Bounds = Mesh->GetBounds();
	return Bounds.BoxExtent * 2.0 * Scale;
}

FBarrageSpawnResult FBarrageSpawnUtils::SpawnEntity(UWorld* World, const FBarrageSpawnParams& Params)
{
	FBarrageSpawnResult Result;
	Result.EntityKey = Params.EntityKey;

	if (!World || !Params.Mesh || !Params.EntityKey.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FBarrageSpawnUtils::SpawnEntity - Invalid parameters"));
		return Result;
	}

	UBarrageDispatch* Physics = World->GetSubsystem<UBarrageDispatch>();
	UFlecsRenderManager* Renderer = UFlecsRenderManager::Get(World);

	if (!Physics)
	{
		UE_LOG(LogTemp, Error, TEXT("FBarrageSpawnUtils::SpawnEntity - No UBarrageDispatch!"));
		return Result;
	}

	// Calculate collider size
	FVector ColliderSize = Params.bAutoCollider
		? CalculateColliderSize(Params.Mesh, Params.MeshScale)
		: Params.ManualColliderSize;

	ColliderSize.X = FMath::Max(ColliderSize.X, 1.0);
	ColliderSize.Y = FMath::Max(ColliderSize.Y, 1.0);
	ColliderSize.Z = FMath::Max(ColliderSize.Z, 1.0);

	FVector Location = Params.WorldTransform.GetLocation();
	FQuat Rotation = Params.WorldTransform.GetRotation();

	// Create physics body
	FBBoxParams BoxParams = FBarrageBounder::GenerateBoxBounds(
		Location,
		ColliderSize.X,
		ColliderSize.Y,
		ColliderSize.Z,
		FVector3d::ZeroVector,
		FMassByCategory::MostEnemies
	);

	// Pass through AllowedDOFs override (0xFF = use layer default)
	BoxParams.AllowedDOFs = Params.AllowedDOFs;

	FBLet Body = Physics->CreatePrimitive(
		BoxParams,
		Params.EntityKey,
		static_cast<uint16>(Params.PhysicsLayer),
		Params.bIsSensor,
		false, // not force dynamic
		Params.bIsMovable,
		Params.Friction,
		Params.Restitution,
		Params.LinearDamping
	);

	if (!FBarragePrimitive::IsNotNull(Body))
	{
		UE_LOG(LogTemp, Error, TEXT("FBarrageSpawnUtils::SpawnEntity - Failed to create physics body!"));
		return Result;
	}

	Result.BarrageKey = Body->KeyIntoBarrage;
	Result.bSuccess = true;

	// Configure physics
	if (!Params.InitialVelocity.IsNearlyZero())
	{
		FBarragePrimitive::SetVelocity(Params.InitialVelocity, Body);
	}
	FBarragePrimitive::SetGravityFactor(Params.GravityFactor, Body);

	if (!Rotation.IsIdentity())
	{
		FBarragePrimitive::ApplyRotation(FQuat4d(Rotation), Body);
	}

	// Add render instance
	if (Renderer)
	{
		FTransform RenderTransform;
		RenderTransform.SetLocation(Location);
		RenderTransform.SetRotation(Rotation);
		RenderTransform.SetScale3D(Params.MeshScale);
		Result.RenderInstanceIndex = Renderer->AddInstance(Params.Mesh, Params.Material, RenderTransform, Params.EntityKey);
	}

	return Result;
}
