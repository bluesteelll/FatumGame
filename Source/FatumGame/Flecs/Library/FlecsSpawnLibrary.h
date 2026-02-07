// Blueprint function library for Flecs ECS spawn operations.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SkeletonTypes.h"
#include "EPhysicsLayer.h"
#include "FlecsConstrainedGroupDefinition.h"
#include "FlecsSpawnLibrary.generated.h"

class UPrimaryDataAsset;
class UStaticMesh;
class UFlecsProjectileDefinition;
class UFlecsEntityDefinition;
class UFlecsConstrainedGroupDefinition;

UCLASS()
class UFlecsSpawnLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// SPAWN (game-thread safe)
	// ═══════════════════════════════════════════════════════════════

	UFUNCTION(BlueprintCallable, Category = "Flecs|Spawn", meta = (WorldContext = "WorldContextObject"))
	static void SpawnWorldItem(
		UObject* WorldContextObject,
		UPrimaryDataAsset* ItemDefinition,
		UStaticMesh* Mesh,
		FVector Location,
		int32 Count = 1,
		float DespawnTime = -1.f,
		EPhysicsLayer PhysicsLayer = EPhysicsLayer::MOVING
	);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Spawn", meta = (WorldContext = "WorldContextObject"))
	static void SpawnDestructible(
		UObject* WorldContextObject,
		UStaticMesh* Mesh,
		FVector Location,
		float MaxHP = 100.f,
		EPhysicsLayer PhysicsLayer = EPhysicsLayer::MOVING
	);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Spawn", meta = (WorldContext = "WorldContextObject"))
	static void SpawnLootableDestructible(
		UObject* WorldContextObject,
		UStaticMesh* Mesh,
		FVector Location,
		float MaxHP = 100.f,
		int32 MinDrops = 1,
		int32 MaxDrops = 3,
		EPhysicsLayer PhysicsLayer = EPhysicsLayer::MOVING
	);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Projectile", meta = (WorldContext = "WorldContextObject"))
	static FSkeletonKey SpawnProjectileFromEntityDef(
		UObject* WorldContextObject,
		UFlecsEntityDefinition* Definition,
		FVector Location,
		FVector Direction,
		float SpeedOverride = 0.f,
		int64 OwnerEntityId = 0
	);

	UE_DEPRECATED(5.7, "Use SpawnProjectileFromEntityDef with UFlecsEntityDefinition instead")
	UFUNCTION(BlueprintCallable, Category = "Flecs|Projectile", meta = (WorldContext = "WorldContextObject", DeprecatedFunction, DeprecationMessage = "Use SpawnProjectileFromEntityDef"))
	static FSkeletonKey SpawnProjectileFromDefinition(
		UObject* WorldContextObject,
		UFlecsProjectileDefinition* Definition,
		FVector Location,
		FVector Direction,
		float SpeedOverride = 0.f
	);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Spawn", meta = (WorldContext = "WorldContextObject"))
	static FFlecsGroupSpawnResult SpawnConstrainedGroup(
		UObject* WorldContextObject,
		UFlecsConstrainedGroupDefinition* Definition,
		FVector Location,
		FRotator Rotation = FRotator::ZeroRotator
	);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Spawn", meta = (WorldContext = "WorldContextObject"))
	static FFlecsGroupSpawnResult SpawnChain(
		UObject* WorldContextObject,
		UStaticMesh* Mesh,
		FVector StartLocation,
		FVector Direction,
		int32 Count = 5,
		float Spacing = 100.f,
		float BreakForce = 0.f,
		float MaxHealth = 0.f
	);

	UFUNCTION(BlueprintCallable, Category = "Flecs|Projectile", meta = (WorldContext = "WorldContextObject"))
	static FSkeletonKey SpawnProjectile(
		UObject* WorldContextObject,
		UStaticMesh* Mesh,
		FVector Location,
		FVector Direction,
		float Speed = 5000.f,
		float Damage = 25.f,
		float GravityFactor = 0.3f,
		float LifetimeSeconds = 10.f,
		float CollisionRadius = 5.f,
		float VisualScale = 1.f,
		bool bIsBouncing = true,
		float Restitution = 0.8f,
		float Friction = 0.2f,
		int32 MaxBounces = -1
	);
};
