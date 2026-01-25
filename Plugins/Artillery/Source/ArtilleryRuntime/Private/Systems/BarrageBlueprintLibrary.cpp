// Full Blueprint integration for Barrage physics system

#include "Systems/BarrageBlueprintLibrary.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FBShapeParams.h"
#include "Skeletonize.h"
#include "DrawDebugHelpers.h"
#include "TransformDispatch.h"
#include "Systems/NiagaraParticleDispatch.h"
#include "PlayerKine.h"  // For BarrageKine
#include <atomic>

#include UE_INLINE_GENERATED_CPP_BY_NAME(BarrageBlueprintLibrary)

namespace
{
	// Global counter for generating unique entity IDs
	std::atomic<uint32> GBlueprintEntityCounter{0};
}

// ═══════════════════════════════════════════════════════════════
// CREATION
// ═══════════════════════════════════════════════════════════════

FSkeletonKey UBarrageBlueprintLibrary::CreateBoxBody(
	UObject* WorldContextObject,
	FVector Location,
	float SizeX,
	float SizeY,
	float SizeZ,
	EPhysicsLayer Layer,
	bool bIsMovable,
	bool bIsSensor)
{
	if (!WorldContextObject)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateBoxBody: Invalid WorldContext"));
		return FSkeletonKey();
	}

	UBarrageDispatch* Physics = WorldContextObject->GetWorld()->GetSubsystem<UBarrageDispatch>();
	if (!Physics)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateBoxBody: UBarrageDispatch not found"));
		return FSkeletonKey();
	}

	// Generate unique key
	const uint32 Id = ++GBlueprintEntityCounter;
	FSkeletonKey EntityKey = FSkeletonKey(FORGE_SKELETON_KEY(Id, SKELLY::SFIX_BAR_PRIM));

	// Create box parameters
	FBBoxParams BoxParams = FBarrageBounder::GenerateBoxBounds(
		Location,
		FMath::Max(SizeX, 1.0),
		FMath::Max(SizeY, 1.0),
		FMath::Max(SizeZ, 1.0),
		FVector3d::ZeroVector,
		FMassByCategory::MostEnemies
	);

	// Create physics body
	FBLet Body = Physics->CreatePrimitive(
		BoxParams,
		EntityKey,
		static_cast<uint16>(Layer),
		bIsSensor,
		false,  // forceDynamic
		bIsMovable
	);

	if (FBarragePrimitive::IsNotNull(Body))
	{
		UE_LOG(LogTemp, Log, TEXT("CreateBoxBody: Created entity %llu at %s"), EntityKey.Obj, *Location.ToString());
		return EntityKey;
	}

	UE_LOG(LogTemp, Error, TEXT("CreateBoxBody: Failed to create physics body"));
	return FSkeletonKey();
}

FSkeletonKey UBarrageBlueprintLibrary::CreateSphereBody(
	UObject* WorldContextObject,
	FVector Location,
	float Radius,
	EPhysicsLayer Layer,
	bool bIsMovable,
	bool bIsSensor)
{
	if (!WorldContextObject)
	{
		return FSkeletonKey();
	}

	UBarrageDispatch* Physics = WorldContextObject->GetWorld()->GetSubsystem<UBarrageDispatch>();
	if (!Physics)
	{
		return FSkeletonKey();
	}

	const uint32 Id = ++GBlueprintEntityCounter;
	FSkeletonKey EntityKey = FSkeletonKey(FORGE_SKELETON_KEY(Id, SKELLY::SFIX_BAR_PRIM));

	FBSphereParams SphereParams = FBarrageBounder::GenerateSphereBounds(Location, Radius);

	FBLet Body = Physics->CreatePrimitive(
		SphereParams,
		EntityKey,
		static_cast<uint16>(Layer),
		bIsSensor
	);

	if (FBarragePrimitive::IsNotNull(Body))
	{
		// Set movability after creation
		if (!bIsMovable)
		{
			// Note: Sphere bodies are always movable in this implementation
		}

		UE_LOG(LogTemp, Log, TEXT("CreateSphereBody: Created entity %llu"), EntityKey.Obj);
		return EntityKey;
	}

	return FSkeletonKey();
}

FSkeletonKey UBarrageBlueprintLibrary::CreateCapsuleBody(
	UObject* WorldContextObject,
	FVector Location,
	float Radius,
	float HalfHeight,
	EPhysicsLayer Layer,
	bool bIsMovable,
	bool bIsSensor)
{
	if (!WorldContextObject)
	{
		return FSkeletonKey();
	}

	UBarrageDispatch* Physics = WorldContextObject->GetWorld()->GetSubsystem<UBarrageDispatch>();
	if (!Physics)
	{
		return FSkeletonKey();
	}

	const uint32 Id = ++GBlueprintEntityCounter;
	FSkeletonKey EntityKey = FSkeletonKey(FORGE_SKELETON_KEY(Id, SKELLY::SFIX_BAR_PRIM));

	FBCapParams CapsuleParams = FBarrageBounder::GenerateCapsuleBounds(
		Location,
		Radius,
		HalfHeight * 2.0f,  // GenerateCapsuleBounds expects full height
		FMassByCategory::MostEnemies,
		FVector3f::ZeroVector
	);

	FBLet Body = Physics->CreatePrimitive(
		CapsuleParams,
		EntityKey,
		static_cast<uint16>(Layer),
		bIsSensor,
		false,
		bIsMovable
	);

	if (FBarragePrimitive::IsNotNull(Body))
	{
		UE_LOG(LogTemp, Log, TEXT("CreateCapsuleBody: Created entity %llu"), EntityKey.Obj);
		return EntityKey;
	}

	return FSkeletonKey();
}

FSkeletonKey UBarrageBlueprintLibrary::CreateFloorBox(
	UObject* WorldContextObject,
	FVector Location,
	float Width,
	float Length,
	float Thickness)
{
	// Create large static box for floor
	return CreateBoxBody(
		WorldContextObject,
		Location,
		Width,
		Length,
		Thickness,
		EPhysicsLayer::NON_MOVING,  // Static!
		false,  // Not movable
		false   // Not sensor
	);
}

// ═══════════════════════════════════════════════════════════════
// BOUNCING PROJECTILES
// ═══════════════════════════════════════════════════════════════

FSkeletonKey UBarrageBlueprintLibrary::CreateBouncingProjectile(
	UObject* WorldContextObject,
	FVector Location,
	float Radius,
	FVector InitialVelocity,
	float Restitution,
	float Friction,
	float GravityFactor)
{
	if (!WorldContextObject)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateBouncingProjectile: Invalid WorldContext"));
		return FSkeletonKey();
	}

	UBarrageDispatch* Physics = WorldContextObject->GetWorld()->GetSubsystem<UBarrageDispatch>();
	UTransformDispatch* TransformDispatch = WorldContextObject->GetWorld()->GetSubsystem<UTransformDispatch>();

	if (!Physics || !TransformDispatch)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateBouncingProjectile: Required subsystems not found"));
		return FSkeletonKey();
	}

	// Generate unique key
	const uint32 Id = ++GBlueprintEntityCounter;
	FSkeletonKey EntityKey = FSkeletonKey(FORGE_SKELETON_KEY(Id, SKELLY::SFIX_BAR_PRIM));

	// Create sphere parameters
	FBSphereParams SphereParams = FBarrageBounder::GenerateSphereBounds(Location, FMath::Max(Radius, 1.0f));

	// Use DEBRIS layer - it only collides with NON_MOVING (environment)
	// This is perfect for bouncing projectiles
	FBLet Body = Physics->CreateBouncingSphere(
		SphereParams,
		EntityKey,
		static_cast<uint16>(EPhysicsLayer::DEBRIS),
		Restitution,
		Friction
	);

	if (FBarragePrimitive::IsNotNull(Body))
	{
		// Set initial velocity
		FBarragePrimitive::SetVelocity(InitialVelocity, Body);

		// Set gravity factor
		FBarragePrimitive::SetGravityFactor(GravityFactor, Body);

		// Register BarrageKine for transform updates (needed for NDC rendering)
		// This allows TransformDispatch to read position from Barrage physics without an Actor
		TWeakObjectPtr<UBarrageDispatch> WeakPhysics(Physics);
		TransformDispatch->RegisterObjectToShadowTransform<BarrageKine, FSkeletonKey, TWeakObjectPtr<UBarrageDispatch>>(
			EntityKey,
			WeakPhysics
		);

		return EntityKey;
	}

	UE_LOG(LogTemp, Error, TEXT("CreateBouncingProjectile: Failed to create physics body"));
	return FSkeletonKey();
}

void UBarrageBlueprintLibrary::RegisterProjectileForNDC(
	UObject* WorldContextObject,
	FSkeletonKey ProjectileKey,
	FName NDCAssetName)
{
	if (!WorldContextObject || !ProjectileKey.IsValid())
	{
		return;
	}

	UNiagaraParticleDispatch* ParticleDispatch = WorldContextObject->GetWorld()->GetSubsystem<UNiagaraParticleDispatch>();
	if (!ParticleDispatch)
	{
		UE_LOG(LogTemp, Warning, TEXT("RegisterProjectileForNDC: NiagaraParticleDispatch not found"));
		return;
	}

	// Check if NDC asset exists for this name, if so register the key
	if (ParticleDispatch->AssetUsesNDCParticles(NDCAssetName))
	{
		TWeakObjectPtr<UNiagaraDataChannelAsset> NDCAsset = ParticleDispatch->GetNDCAssetForProjectileDefinition(NDCAssetName);
		ParticleDispatch->RegisterKeyForProcessing(NDCAssetName, ProjectileKey, NDCAsset);
	}
	else
	{
		// NDC asset not registered yet - the projectile will still have physics,
		// but won't be rendered until NDC asset is added via AddNDCReference
		UE_LOG(LogTemp, Warning, TEXT("RegisterProjectileForNDC: No NDC asset registered for '%s'. Projectile will have physics but no visual until NDC is set up."), *NDCAssetName.ToString());
	}
}

void UBarrageBlueprintLibrary::SpawnBouncingProjectileSpread(
	UObject* WorldContextObject,
	FVector Location,
	FVector Direction,
	float Speed,
	int32 Count,
	float SpreadAngle,
	float Radius,
	float Restitution,
	FName NDCAssetName,
	TArray<FSkeletonKey>& OutProjectileKeys)
{
	OutProjectileKeys.Empty();

	if (!WorldContextObject || Count <= 0)
	{
		return;
	}

	Direction = Direction.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = FVector::ForwardVector;
	}

	// Calculate perpendicular vectors for spread
	FVector Right = FVector::CrossProduct(Direction, FVector::UpVector);
	if (Right.IsNearlyZero())
	{
		Right = FVector::CrossProduct(Direction, FVector::RightVector);
	}
	Right.Normalize();
	FVector Up = FVector::CrossProduct(Right, Direction);
	Up.Normalize();

	float HalfAngleRad = FMath::DegreesToRadians(SpreadAngle * 0.5f);

	for (int32 i = 0; i < Count; ++i)
	{
		// Random spread within cone
		float RandomAngle = FMath::FRandRange(0.0f, 2.0f * PI);
		float RandomRadius = FMath::FRandRange(0.0f, FMath::Tan(HalfAngleRad));

		FVector SpreadOffset = (Right * FMath::Cos(RandomAngle) + Up * FMath::Sin(RandomAngle)) * RandomRadius;
		FVector SpreadDirection = (Direction + SpreadOffset).GetSafeNormal();
		FVector Velocity = SpreadDirection * Speed;

		FSkeletonKey Key = CreateBouncingProjectile(
			WorldContextObject,
			Location,
			Radius,
			Velocity,
			Restitution,
			0.2f,  // Default friction
			0.3f   // Low gravity for projectiles
		);

		if (Key.IsValid())
		{
			OutProjectileKeys.Add(Key);
			RegisterProjectileForNDC(WorldContextObject, Key, NDCAssetName);
		}
	}
}

FSkeletonKey UBarrageBlueprintLibrary::LoadComplexMeshCollision(
	UObject* WorldContextObject,
	UStaticMeshComponent* StaticMeshComponent,
	FTransform Transform)
{
	if (!WorldContextObject || !StaticMeshComponent)
	{
		return FSkeletonKey();
	}

	UBarrageDispatch* Physics = WorldContextObject->GetWorld()->GetSubsystem<UBarrageDispatch>();
	if (!Physics)
	{
		return FSkeletonKey();
	}

	const uint32 Id = ++GBlueprintEntityCounter;
	FSkeletonKey EntityKey = FSkeletonKey(FORGE_SKELETON_KEY(Id, SKELLY::SFIX_BAR_PRIM));

	FBTransform MeshTransform(Transform);
	FBLet Body = Physics->LoadComplexStaticMesh(MeshTransform, StaticMeshComponent, EntityKey);

	if (FBarragePrimitive::IsNotNull(Body))
	{
		UE_LOG(LogTemp, Log, TEXT("LoadComplexMeshCollision: Created complex mesh entity %llu"), EntityKey.Obj);
		return EntityKey;
	}

	return FSkeletonKey();
}

// ═══════════════════════════════════════════════════════════════
// VELOCITY & FORCES
// ═══════════════════════════════════════════════════════════════

bool UBarrageBlueprintLibrary::SetVelocity(FSkeletonKey EntityKey, FVector Velocity)
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return false;
	}

	FBLet Body = Physics->GetShapeRef(EntityKey);
	if (!FBarragePrimitive::IsNotNull(Body))
	{
		return false;
	}

	FBarragePrimitive::SetVelocity(Velocity, Body);
	return true;
}

bool UBarrageBlueprintLibrary::GetVelocity(FSkeletonKey EntityKey, FVector& Velocity)
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return false;
	}

	FBLet Body = Physics->GetShapeRef(EntityKey);
	if (!FBarragePrimitive::IsNotNull(Body))
	{
		return false;
	}

	Velocity = FVector(FBarragePrimitive::GetVelocity(Body));
	return true;
}

bool UBarrageBlueprintLibrary::ApplyForce(FSkeletonKey EntityKey, FVector Force)
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return false;
	}

	FBLet Body = Physics->GetShapeRef(EntityKey);
	if (!FBarragePrimitive::IsNotNull(Body))
	{
		return false;
	}

	FBarragePrimitive::ApplyForce(Force, Body);
	return true;
}

bool UBarrageBlueprintLibrary::ApplyImpulse(FSkeletonKey EntityKey, FVector Impulse)
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return false;
	}

	FBLet Body = Physics->GetShapeRef(EntityKey);
	if (!FBarragePrimitive::IsNotNull(Body))
	{
		return false;
	}

	// Apply impulse as instant velocity change
	FVector3f CurrentVel = FBarragePrimitive::GetVelocity(Body);
	FBarragePrimitive::SetVelocity(FVector(CurrentVel) + Impulse, Body);
	return true;
}

bool UBarrageBlueprintLibrary::ApplyTorque(FSkeletonKey EntityKey, FVector Torque)
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return false;
	}

	FBLet Body = Physics->GetShapeRef(EntityKey);
	if (!FBarragePrimitive::IsNotNull(Body))
	{
		return false;
	}

	FBarragePrimitive::ApplyTorque(Torque, Body);
	return true;
}

bool UBarrageBlueprintLibrary::SetSpeedLimit(FSkeletonKey EntityKey, float MaxSpeed)
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return false;
	}

	FBLet Body = Physics->GetShapeRef(EntityKey);
	if (!FBarragePrimitive::IsNotNull(Body))
	{
		return false;
	}

	FBarragePrimitive::SpeedLimit(Body, MaxSpeed);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// POSITION & ROTATION
// ═══════════════════════════════════════════════════════════════

bool UBarrageBlueprintLibrary::SetPosition(FSkeletonKey EntityKey, FVector NewLocation)
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return false;
	}

	FBLet Body = Physics->GetShapeRef(EntityKey);
	if (!FBarragePrimitive::IsNotNull(Body))
	{
		return false;
	}

	FBarragePrimitive::SetPosition(NewLocation, Body);
	return true;
}

bool UBarrageBlueprintLibrary::GetPosition(FSkeletonKey EntityKey, FVector& Location)
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return false;
	}

	FBLet Body = Physics->GetShapeRef(EntityKey);
	if (!FBarragePrimitive::IsNotNull(Body))
	{
		return false;
	}

	Location = FVector(FBarragePrimitive::GetPosition(Body));
	return true;
}

bool UBarrageBlueprintLibrary::SetRotation(FSkeletonKey EntityKey, FRotator NewRotation)
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return false;
	}

	FBLet Body = Physics->GetShapeRef(EntityKey);
	if (!FBarragePrimitive::IsNotNull(Body))
	{
		return false;
	}

	FBarragePrimitive::ApplyRotation(FQuat4d(NewRotation.Quaternion()), Body);
	return true;
}

bool UBarrageBlueprintLibrary::GetRotation(FSkeletonKey EntityKey, FRotator& Rotation)
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return false;
	}

	FBLet Body = Physics->GetShapeRef(EntityKey);
	if (!FBarragePrimitive::IsNotNull(Body))
	{
		return false;
	}

	FQuat4f Quat = FBarragePrimitive::OptimisticGetAbsoluteRotation(Body);
	Rotation = FQuat(Quat).Rotator();
	return true;
}

bool UBarrageBlueprintLibrary::GetTransform(FSkeletonKey EntityKey, FTransform& Transform)
{
	FVector Location;
	FRotator Rotation;

	if (GetPosition(EntityKey, Location) && GetRotation(EntityKey, Rotation))
	{
		Transform.SetLocation(Location);
		Transform.SetRotation(Rotation.Quaternion());
		Transform.SetScale3D(FVector::OneVector);
		return true;
	}

	return false;
}

// ═══════════════════════════════════════════════════════════════
// GRAVITY & PROPERTIES
// ═══════════════════════════════════════════════════════════════

bool UBarrageBlueprintLibrary::SetGravityFactor(FSkeletonKey EntityKey, float GravityFactor)
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return false;
	}

	FBLet Body = Physics->GetShapeRef(EntityKey);
	if (!FBarragePrimitive::IsNotNull(Body))
	{
		return false;
	}

	FBarragePrimitive::SetGravityFactor(GravityFactor, Body);
	return true;
}

bool UBarrageBlueprintLibrary::SetCharacterGravity(FSkeletonKey EntityKey, FVector CustomGravity)
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return false;
	}

	FBLet Body = Physics->GetShapeRef(EntityKey);
	if (!FBarragePrimitive::IsNotNull(Body))
	{
		return false;
	}

	FBarragePrimitive::SetCharacterGravity(CustomGravity, Body);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// RAYCASTING & QUERIES
// ═══════════════════════════════════════════════════════════════

bool UBarrageBlueprintLibrary::Raycast(
	UObject* WorldContextObject,
	FVector Start,
	FVector End,
	EPhysicsLayer Layer,
	FHitResult& HitResult,
	const FSkeletonKey& IgnoreEntity)
{
	if (!WorldContextObject)
	{
		return false;
	}

	UBarrageDispatch* Physics = WorldContextObject->GetWorld()->GetSubsystem<UBarrageDispatch>();
	if (!Physics)
	{
		return false;
	}

	FVector Direction = (End - Start).GetSafeNormal();
	TSharedPtr<FHitResult> HitPtr = MakeShared<FHitResult>();

	// Get filter
	auto BroadPhaseFilter = Physics->GetDefaultBroadPhaseLayerFilter(static_cast<Layers::EJoltPhysicsLayer>(Layer));
	auto ObjectFilter = Physics->GetDefaultLayerFilter(static_cast<Layers::EJoltPhysicsLayer>(Layer));

	// Add ignore filter if needed
	if (IgnoreEntity.IsValid())
	{
		FBLet IgnoreBody = Physics->GetShapeRef(IgnoreEntity);
		if (FBarragePrimitive::IsNotNull(IgnoreBody))
		{
			Physics->CastRay(Start, Direction, BroadPhaseFilter, ObjectFilter,
				Physics->GetFilterToIgnoreSingleBody(IgnoreBody), HitPtr);
		}
		else
		{
			Physics->CastRay(Start, Direction, BroadPhaseFilter, ObjectFilter,
				JPH::BodyFilter(), HitPtr);
		}
	}
	else
	{
		Physics->CastRay(Start, Direction, BroadPhaseFilter, ObjectFilter,
			JPH::BodyFilter(), HitPtr);
	}

	if (HitPtr->bBlockingHit)
	{
		HitResult = *HitPtr;
		return true;
	}

	return false;
}

bool UBarrageBlueprintLibrary::SphereCast(
	UObject* WorldContextObject,
	FVector Start,
	FVector End,
	float Radius,
	EPhysicsLayer Layer,
	FHitResult& HitResult,
	const FSkeletonKey& IgnoreEntity)
{
	if (!WorldContextObject)
	{
		return false;
	}

	UBarrageDispatch* Physics = WorldContextObject->GetWorld()->GetSubsystem<UBarrageDispatch>();
	if (!Physics)
	{
		return false;
	}

	FVector Direction = (End - Start).GetSafeNormal();
	double Distance = FVector::Dist(Start, End);
	TSharedPtr<FHitResult> HitPtr = MakeShared<FHitResult>();

	auto BroadPhaseFilter = Physics->GetDefaultBroadPhaseLayerFilter(static_cast<Layers::EJoltPhysicsLayer>(Layer));
	auto ObjectFilter = Physics->GetDefaultLayerFilter(static_cast<Layers::EJoltPhysicsLayer>(Layer));

	// Add ignore filter if needed
	if (IgnoreEntity.IsValid())
	{
		FBLet IgnoreBody = Physics->GetShapeRef(IgnoreEntity);
		if (FBarragePrimitive::IsNotNull(IgnoreBody))
		{
			Physics->SphereCast(Radius, Distance, Start, Direction, HitPtr,
				BroadPhaseFilter, ObjectFilter,
				Physics->GetFilterToIgnoreSingleBody(IgnoreBody), 0);
		}
		else
		{
			Physics->SphereCast(Radius, Distance, Start, Direction, HitPtr,
				BroadPhaseFilter, ObjectFilter, JPH::BodyFilter(), 0);
		}
	}
	else
	{
		Physics->SphereCast(Radius, Distance, Start, Direction, HitPtr,
			BroadPhaseFilter, ObjectFilter, JPH::BodyFilter(), 0);
	}

	if (HitPtr->bBlockingHit)
	{
		HitResult = *HitPtr;
		return true;
	}

	return false;
}

int32 UBarrageBlueprintLibrary::SphereSearch(
	UObject* WorldContextObject,
	FVector Location,
	float Radius,
	EPhysicsLayer Layer,
	TArray<FSkeletonKey>& FoundEntities)
{
	FoundEntities.Empty();

	if (!WorldContextObject)
	{
		return 0;
	}

	UBarrageDispatch* Physics = WorldContextObject->GetWorld()->GetSubsystem<UBarrageDispatch>();
	if (!Physics)
	{
		return 0;
	}

	uint32 FoundCount = 0;
	TArray<uint32> FoundBodyIDs;

	auto BroadPhaseFilter = Physics->GetDefaultBroadPhaseLayerFilter(static_cast<Layers::EJoltPhysicsLayer>(Layer));
	auto ObjectFilter = Physics->GetDefaultLayerFilter(static_cast<Layers::EJoltPhysicsLayer>(Layer));

	Physics->SphereSearch(
		FBarrageKey(0),  // No source body
		Location,
		Radius,
		BroadPhaseFilter,
		ObjectFilter,
		JPH::BodyFilter(),
		&FoundCount,
		FoundBodyIDs
	);

	// Convert BodyIDs to SkeletonKeys
	for (uint32 i = 0; i < FoundCount; ++i)
	{
		FBarrageKey BarrageKey = Physics->GenerateBarrageKeyFromBodyId(FoundBodyIDs[i]);
		FBLet Body = Physics->GetShapeRef(BarrageKey);

		if (FBarragePrimitive::IsNotNull(Body))
		{
			FoundEntities.Add(Body->KeyOutOfBarrage);
		}
	}

	return FoundEntities.Num();
}

// ═══════════════════════════════════════════════════════════════
// DELETION & LIFECYCLE
// ═══════════════════════════════════════════════════════════════

bool UBarrageBlueprintLibrary::DestroyEntity(FSkeletonKey EntityKey)
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return false;
	}

	FBLet Body = Physics->GetShapeRef(EntityKey);
	if (!FBarragePrimitive::IsNotNull(Body))
	{
		return false;
	}

	// Use safe tombstoning
	return Physics->SuggestTombstone(Body) != 1;
}

bool UBarrageBlueprintLibrary::IsEntityValid(FSkeletonKey EntityKey)
{
	UBarrageDispatch* Physics = UBarrageDispatch::SelfPtr;
	if (!Physics)
	{
		return false;
	}

	FBLet Body = Physics->GetShapeRef(EntityKey);
	return FBarragePrimitive::IsNotNull(Body);
}

// ═══════════════════════════════════════════════════════════════
// DEBUGGING
// ═══════════════════════════════════════════════════════════════

int32 UBarrageBlueprintLibrary::GetTotalBodyCount(UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return 0;
	}

	UBarrageDispatch* Physics = WorldContextObject->GetWorld()->GetSubsystem<UBarrageDispatch>();
	if (!Physics)
	{
		return 0;
	}

	return Physics->GetBodyCount();
}

void UBarrageBlueprintLibrary::DrawDebugPhysicsBody(
	UObject* WorldContextObject,
	FSkeletonKey EntityKey,
	FLinearColor Color,
	float Thickness)
{
	if (!WorldContextObject)
	{
		return;
	}

	UBarrageDispatch* Physics = WorldContextObject->GetWorld()->GetSubsystem<UBarrageDispatch>();
	if (!Physics)
	{
		return;
	}

	FBLet Body = Physics->GetShapeRef(EntityKey);
	if (!FBarragePrimitive::IsNotNull(Body))
	{
		return;
	}

	FVector Position = FVector(FBarragePrimitive::GetPosition(Body));

	// Draw simple box for visualization
	// TODO: Get actual shape bounds from Jolt
	FVector BoxExtent(50, 50, 50);

	DrawDebugBox(
		WorldContextObject->GetWorld(),
		Position,
		BoxExtent,
		Color.ToFColor(true),
		false,  // Persistent
		0.0f,   // Lifetime (one frame)
		0,      // Depth priority
		Thickness
	);
}
