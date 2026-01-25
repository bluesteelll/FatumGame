// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "Systems/ArtilleryBPLibs.h"
#include "Systems/AInstancedMeshManager.h"
#include "Systems/NiagaraParticleDispatch.h"
#include "BarrageDispatch.h"
#include "TransformDispatch.h"
#include "EPhysicsLayer.h"

// Static storage for dynamically registered projectile definitions
namespace
{
	TMap<FName, TWeakObjectPtr<AInstancedMeshManager>> DynamicProjectileMeshManagers;
}

bool UArtilleryLibrary::RegisterProjectileDefinition(UObject* WorldContextObject, UProjectileDefinition* Definition)
{
	if (!WorldContextObject || !Definition || !Definition->ProjectileMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("RegisterProjectileDefinition: Invalid parameters"));
		return false;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return false;
	}

	// Check if already registered
	if (DynamicProjectileMeshManagers.Contains(Definition->ProjectileName))
	{
		TWeakObjectPtr<AInstancedMeshManager> Existing = DynamicProjectileMeshManagers.FindRef(Definition->ProjectileName);
		if (Existing.IsValid())
		{
			return true; // Already registered
		}
	}

	// Create mesh manager for this projectile type
	AInstancedMeshManager* NewMeshManager = World->SpawnActor<AInstancedMeshManager>();
	if (!NewMeshManager)
	{
		UE_LOG(LogTemp, Error, TEXT("RegisterProjectileDefinition: Failed to spawn mesh manager"));
		return false;
	}

	NewMeshManager->InitializeManager();
	NewMeshManager->SetStaticMesh(Definition->ProjectileMesh);
	NewMeshManager->SetInternalFlags(EInternalObjectFlags::Async);
	NewMeshManager->SwarmKineManager->SetCanEverAffectNavigation(false);
	NewMeshManager->SwarmKineManager->SetSimulatePhysics(false);
	NewMeshManager->SwarmKineManager->bNavigationRelevant = 0;
	NewMeshManager->SwarmKineManager->SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);
	NewMeshManager->SwarmKineManager->SetInternalFlags(EInternalObjectFlags::Async);

#if WITH_EDITOR
	NewMeshManager->SwarmKineManager->OnMeshRebuild(true);
#endif
	NewMeshManager->SwarmKineManager->ReregisterComponent();

	// Register in our map
	DynamicProjectileMeshManagers.Add(Definition->ProjectileName, NewMeshManager);

	// Register NDC if specified
	if (Definition->ParticleEffectDataChannel)
	{
		UNiagaraParticleDispatch* NPD = World->GetSubsystem<UNiagaraParticleDispatch>();
		if (NPD)
		{
			NPD->AddNDCReference(Definition->ProjectileName, Definition->ParticleEffectDataChannel);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("RegisterProjectileDefinition: Registered '%s'"), *Definition->ProjectileName.ToString());
	return true;
}

FSkeletonKey UArtilleryLibrary::SpawnProjectileFromDefinition(
	UObject* WorldContextObject,
	UProjectileDefinition* Definition,
	FVector SpawnLocation,
	FVector Direction,
	float SpeedOverride,
	FGunKey GunKey)
{
	if (!WorldContextObject || !Definition)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpawnProjectileFromDefinition: Invalid parameters"));
		return FSkeletonKey();
	}

	// Auto-register if not already
	if (!DynamicProjectileMeshManagers.Contains(Definition->ProjectileName) ||
		!DynamicProjectileMeshManagers.FindRef(Definition->ProjectileName).IsValid())
	{
		if (!RegisterProjectileDefinition(WorldContextObject, Definition))
		{
			return FSkeletonKey();
		}
	}

	TWeakObjectPtr<AInstancedMeshManager> MeshManagerPtr = DynamicProjectileMeshManagers.FindRef(Definition->ProjectileName);
	if (!MeshManagerPtr.IsValid())
	{
		return FSkeletonKey();
	}

	UWorld* World = WorldContextObject->GetWorld();
	AInstancedMeshManager* MeshManager = MeshManagerPtr.Get();

	// Calculate velocity
	Direction = Direction.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = FVector::ForwardVector;
	}
	float Speed = SpeedOverride > 0.0f ? SpeedOverride : Definition->DefaultSpeed;
	FVector Velocity = Direction * Speed;

	// Create transform
	FTransform WorldTransform;
	WorldTransform.SetLocation(SpawnLocation);
	WorldTransform.SetRotation(Direction.ToOrientationQuat());
	WorldTransform.SetScale3D(FVector(Definition->VisualScale));

	FSkeletonKey NewKey;

	if (Definition->bIsBouncing)
	{
		// Bouncing projectile - uses sphere collision with restitution
		NewKey = MeshManager->CreateNewBouncingInstance(
			WorldTransform,
			Velocity,
			static_cast<uint16_t>(Definition->CollisionLayer),
			Definition->CollisionRadius,
			Definition->Restitution,
			Definition->Friction,
			Definition->GravityFactor,
			Definition->VisualScale
		);
	}
	else
	{
		// Standard projectile - sensor, destroyed on hit
		NewKey = MeshManager->CreateNewInstance(
			WorldTransform,
			Velocity,
			static_cast<uint16_t>(Definition->CollisionLayer),
			Definition->VisualScale
		);
	}

	if (!NewKey.IsValid())
	{
		return FSkeletonKey();
	}

	// Register for NDC rendering if available
	if (Definition->ParticleEffectDataChannel)
	{
		UNiagaraParticleDispatch* NPD = World->GetSubsystem<UNiagaraParticleDispatch>();
		if (NPD)
		{
			TWeakObjectPtr<UNiagaraDataChannelAsset> NDCAsset = NPD->GetNDCAssetForProjectileDefinition(Definition->ProjectileName);
			if (NDCAsset.IsValid())
			{
				NPD->RegisterKeyForProcessing(Definition->ProjectileName, NewKey, NDCAsset);
			}
		}
	}

	// Set up lifetime
	if (Definition->LifetimeSeconds > 0.0f && UArtilleryDispatch::SelfPtr)
	{
		int LifeTicks = static_cast<int>(Definition->LifetimeSeconds * ArtilleryTickHertz);
		UArtilleryDispatch::SelfPtr->REGISTER_PROJECTILE_FINAL_TICK_RESOLVER(LifeTicks, NewKey);
	}

	return NewKey;
}

TArray<FSkeletonKey> UArtilleryLibrary::SpawnProjectileSpreadFromDefinition(
	UObject* WorldContextObject,
	UProjectileDefinition* Definition,
	FVector SpawnLocation,
	FVector Direction,
	int32 Count,
	float SpreadAngle,
	float SpeedOverride,
	FGunKey GunKey)
{
	TArray<FSkeletonKey> Result;

	if (!WorldContextObject || !Definition || Count <= 0)
	{
		return Result;
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

		FSkeletonKey Key = SpawnProjectileFromDefinition(
			WorldContextObject,
			Definition,
			SpawnLocation,
			SpreadDirection,
			SpeedOverride,
			GunKey
		);

		if (Key.IsValid())
		{
			Result.Add(Key);
		}
	}

	return Result;
}
