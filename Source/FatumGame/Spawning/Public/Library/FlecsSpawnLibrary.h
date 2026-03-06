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

	UE_DEPRECATED(5.7, "Use UFlecsEntityLibrary::SpawnEntity with UFlecsEntityDefinition instead")
	UFUNCTION(BlueprintCallable, Category = "Flecs|Spawn", meta = (WorldContext = "WorldContextObject", DeprecatedFunction, DeprecationMessage = "Use UFlecsEntityLibrary::SpawnEntity with UFlecsEntityDefinition"))
	static void SpawnWorldItem(
		UObject* WorldContextObject,
		UPrimaryDataAsset* ItemDefinition,
		UStaticMesh* Mesh,
		FVector Location,
		int32 Count = 1,
		float DespawnTime = -1.f,
		EPhysicsLayer PhysicsLayer = EPhysicsLayer::MOVING
	);

	UE_DEPRECATED(5.7, "Use UFlecsEntityLibrary::SpawnEntity with UFlecsEntityDefinition instead")
	UFUNCTION(BlueprintCallable, Category = "Flecs|Spawn", meta = (WorldContext = "WorldContextObject", DeprecatedFunction, DeprecationMessage = "Use UFlecsEntityLibrary::SpawnEntity with UFlecsEntityDefinition"))
	static void SpawnDestructible(
		UObject* WorldContextObject,
		UStaticMesh* Mesh,
		FVector Location,
		float MaxHP = 100.f,
		EPhysicsLayer PhysicsLayer = EPhysicsLayer::MOVING
	);

	UE_DEPRECATED(5.7, "Use UFlecsEntityLibrary::SpawnEntity with UFlecsEntityDefinition instead")
	UFUNCTION(BlueprintCallable, Category = "Flecs|Spawn", meta = (WorldContext = "WorldContextObject", DeprecatedFunction, DeprecationMessage = "Use UFlecsEntityLibrary::SpawnEntity with UFlecsEntityDefinition"))
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
};
