// Unified entity spawning API for Flecs ECS.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SkeletonTypes.h"
#include "FlecsEntitySpawner.generated.h"

// Forward declarations
class UFlecsEntityDefinition;
class UFlecsItemDefinition;
class UFlecsPhysicsProfile;
class UFlecsRenderProfile;
class UFlecsHealthProfile;
class UFlecsDamageProfile;
class UFlecsProjectileProfile;
class UFlecsContainerProfile;
class UFlecsArtillerySubsystem;

/**
 * Entity spawn request - composable profiles for creating Flecs entities.
 *
 * Usage (Blueprint):
 *   1. Create FEntitySpawnRequest variable
 *   2. Set Location and desired profiles
 *   3. Call UFlecsEntityLibrary::SpawnEntity()
 *
 * Usage (C++ fluent):
 *   FSkeletonKey Key = FEntitySpawnRequest::At(Location)
 *       .WithItem(ItemDef, 5)
 *       .WithPhysics(PhysicsProfile)
 *       .WithRender(RenderProfile)
 *       .Pickupable()
 *       .Spawn(WorldContext);
 *
 * Composition examples:
 *   Item in inventory:    Item only
 *   Item in world:        Item + Physics + Render + Pickupable
 *   Projectile:           Projectile + Damage + Physics + Render
 *   Destructible box:     Health + Physics + Render + Destructible
 *   Chest:                Container + Physics + Render
 *   Player inventory:     Container only
 *   Invisible trigger:    Physics(Sensor) only
 */
USTRUCT(BlueprintType)
struct FATUMGAME_API FEntitySpawnRequest
{
	GENERATED_BODY()

	// ═══════════════════════════════════════════════════════════════
	// TRANSFORM
	// ═══════════════════════════════════════════════════════════════

	/** World location to spawn at */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FVector Location = FVector::ZeroVector;

	/** World rotation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Initial velocity (for physics entities) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
	FVector InitialVelocity = FVector::ZeroVector;

	// ═══════════════════════════════════════════════════════════════
	// UNIFIED DEFINITION (optional - sets all profiles at once)
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Unified entity definition - a preset combining multiple profiles.
	 * If set, profiles from this definition are used (unless explicitly overridden below).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Definition")
	TObjectPtr<UFlecsEntityDefinition> EntityDefinition;

	// ═══════════════════════════════════════════════════════════════
	// PROFILES (composition) - override EntityDefinition if set
	// ═══════════════════════════════════════════════════════════════

	/** Item definition - makes entity an item */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profiles")
	TObjectPtr<UFlecsItemDefinition> ItemDefinition;

	/** Physics profile - adds collision and physics simulation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profiles")
	TObjectPtr<UFlecsPhysicsProfile> PhysicsProfile;

	/** Render profile - adds visual mesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profiles")
	TObjectPtr<UFlecsRenderProfile> RenderProfile;

	/** Health profile - makes entity damageable */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profiles")
	TObjectPtr<UFlecsHealthProfile> HealthProfile;

	/** Damage profile - makes entity deal contact damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profiles")
	TObjectPtr<UFlecsDamageProfile> DamageProfile;

	/** Projectile profile - makes entity a projectile with lifetime */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profiles")
	TObjectPtr<UFlecsProjectileProfile> ProjectileProfile;

	/** Container profile - makes entity a container for items */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profiles")
	TObjectPtr<UFlecsContainerProfile> ContainerProfile;

	// ═══════════════════════════════════════════════════════════════
	// INLINE OVERRIDES (use when no Data Asset needed)
	// ═══════════════════════════════════════════════════════════════

	/** Item count (for ItemDefinition) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overrides", meta = (ClampMin = "1"))
	int32 ItemCount = 1;

	/** Auto-despawn time in seconds (-1 = never) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overrides")
	float DespawnTime = -1.f;

	/** Owner entity key (for containers) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overrides")
	FSkeletonKey OwnerKey;

	/** Owner Flecs entity ID (for projectiles - friendly fire check) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overrides")
	int64 OwnerEntityId = 0;

	// ═══════════════════════════════════════════════════════════════
	// TAGS
	// ═══════════════════════════════════════════════════════════════

	/** Can be picked up by characters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags")
	bool bPickupable = false;

	/** Can be destroyed by damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags")
	bool bDestructible = false;

	/** Drops loot on death */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags")
	bool bHasLoot = false;

	/** Is a character entity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags")
	bool bIsCharacter = false;

	// ═══════════════════════════════════════════════════════════════
	// C++ FLUENT BUILDER
	// ═══════════════════════════════════════════════════════════════

	/** Create request at location */
	static FEntitySpawnRequest At(FVector InLocation)
	{
		FEntitySpawnRequest Request;
		Request.Location = InLocation;
		return Request;
	}

	/** Create request at location with rotation */
	static FEntitySpawnRequest At(FVector InLocation, FRotator InRotation)
	{
		FEntitySpawnRequest Request;
		Request.Location = InLocation;
		Request.Rotation = InRotation;
		return Request;
	}

	/** Create request from unified entity definition */
	static FEntitySpawnRequest FromDefinition(UFlecsEntityDefinition* Definition, FVector InLocation)
	{
		FEntitySpawnRequest Request;
		Request.EntityDefinition = Definition;
		Request.Location = InLocation;
		return Request;
	}

	/** Create request from unified entity definition with rotation */
	static FEntitySpawnRequest FromDefinition(UFlecsEntityDefinition* Definition, FVector InLocation, FRotator InRotation)
	{
		FEntitySpawnRequest Request;
		Request.EntityDefinition = Definition;
		Request.Location = InLocation;
		Request.Rotation = InRotation;
		return Request;
	}

	/** Set unified entity definition (applies all profiles from definition) */
	FEntitySpawnRequest& WithDefinition(UFlecsEntityDefinition* Definition)
	{
		EntityDefinition = Definition;
		return *this;
	}

	/** Add item component */
	FEntitySpawnRequest& WithItem(UFlecsItemDefinition* Def, int32 Count = 1)
	{
		ItemDefinition = Def;
		ItemCount = Count;
		return *this;
	}

	/** Add physics component */
	FEntitySpawnRequest& WithPhysics(UFlecsPhysicsProfile* Profile)
	{
		PhysicsProfile = Profile;
		return *this;
	}

	/** Add render component */
	FEntitySpawnRequest& WithRender(UFlecsRenderProfile* Profile)
	{
		RenderProfile = Profile;
		return *this;
	}

	/** Add health component */
	FEntitySpawnRequest& WithHealth(UFlecsHealthProfile* Profile)
	{
		HealthProfile = Profile;
		return *this;
	}

	/** Add damage component */
	FEntitySpawnRequest& WithDamage(UFlecsDamageProfile* Profile)
	{
		DamageProfile = Profile;
		return *this;
	}

	/** Add projectile component */
	FEntitySpawnRequest& WithProjectile(UFlecsProjectileProfile* Profile)
	{
		ProjectileProfile = Profile;
		return *this;
	}

	/** Add container component */
	FEntitySpawnRequest& WithContainer(UFlecsContainerProfile* Profile)
	{
		ContainerProfile = Profile;
		return *this;
	}

	/** Set initial velocity */
	FEntitySpawnRequest& WithVelocity(FVector Velocity)
	{
		InitialVelocity = Velocity;
		return *this;
	}

	/** Set owner (SkeletonKey, for containers) */
	FEntitySpawnRequest& WithOwner(FSkeletonKey Owner)
	{
		OwnerKey = Owner;
		return *this;
	}

	/** Set owner entity ID (Flecs ID, for projectiles - friendly fire) */
	FEntitySpawnRequest& WithOwnerEntity(int64 OwnerId)
	{
		OwnerEntityId = OwnerId;
		return *this;
	}

	/** Set despawn time */
	FEntitySpawnRequest& WithDespawn(float Seconds)
	{
		DespawnTime = Seconds;
		return *this;
	}

	/** Make pickupable */
	FEntitySpawnRequest& Pickupable()
	{
		bPickupable = true;
		return *this;
	}

	/** Make destructible */
	FEntitySpawnRequest& Destructible()
	{
		bDestructible = true;
		return *this;
	}

	/** Add loot drops */
	FEntitySpawnRequest& HasLoot()
	{
		bHasLoot = true;
		return *this;
	}

	/** Mark as character */
	FEntitySpawnRequest& AsCharacter()
	{
		bIsCharacter = true;
		return *this;
	}

	/** Spawn the entity (convenience, calls UFlecsEntityLibrary::SpawnEntity) */
	FSkeletonKey Spawn(UObject* WorldContext) const;

	// ═══════════════════════════════════════════════════════════════
	// VALIDATION
	// ═══════════════════════════════════════════════════════════════

	/** Check if request has any profiles set (either via EntityDefinition or individual profiles) */
	bool HasAnyProfile() const
	{
		if (EntityDefinition != nullptr)
		{
			return true;
		}
		return ItemDefinition != nullptr
			|| PhysicsProfile != nullptr
			|| RenderProfile != nullptr
			|| HealthProfile != nullptr
			|| DamageProfile != nullptr
			|| ProjectileProfile != nullptr
			|| ContainerProfile != nullptr;
	}

	/** Check if request will create a world entity (physics or render) */
	bool IsWorldEntity() const;
};

/**
 * Blueprint function library for unified entity spawning.
 *
 * All entity types (items, projectiles, containers, characters) are spawned
 * through this single API using composition of profiles.
 */
UCLASS()
class FATUMGAME_API UFlecsEntityLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// SPAWNING
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Spawn a Flecs entity with the specified profiles.
	 *
	 * @param WorldContextObject World context
	 * @param Request Spawn request with profiles and settings
	 * @return SkeletonKey of the spawned entity (invalid if failed)
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Entity", meta = (WorldContext = "WorldContextObject"))
	static FSkeletonKey SpawnEntity(
		UObject* WorldContextObject,
		const FEntitySpawnRequest& Request);

	/**
	 * Spawn a Flecs entity from a unified entity definition.
	 * Convenience function - same as SpawnEntity with FEntitySpawnRequest::FromDefinition().
	 *
	 * @param WorldContextObject World context
	 * @param Definition Entity definition (preset of profiles)
	 * @param Location World location to spawn at
	 * @param Rotation World rotation (optional)
	 * @return SkeletonKey of the spawned entity (invalid if failed)
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Entity", meta = (WorldContext = "WorldContextObject"))
	static FSkeletonKey SpawnEntityFromDefinition(
		UObject* WorldContextObject,
		UFlecsEntityDefinition* Definition,
		FVector Location,
		FRotator Rotation = FRotator::ZeroRotator);

	/**
	 * Spawn multiple entities from an array of requests.
	 * More efficient than calling SpawnEntity in a loop.
	 *
	 * @param WorldContextObject World context
	 * @param Requests Array of spawn requests
	 * @return Array of SkeletonKeys (invalid keys for failed spawns)
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Entity", meta = (WorldContext = "WorldContextObject"))
	static TArray<FSkeletonKey> SpawnEntities(
		UObject* WorldContextObject,
		const TArray<FEntitySpawnRequest>& Requests);

	// ═══════════════════════════════════════════════════════════════
	// DESTRUCTION
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Destroy an entity by its SkeletonKey.
	 * Handles cleanup of physics, render, and Flecs entity.
	 *
	 * @param WorldContextObject World context
	 * @param EntityKey Key of entity to destroy
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Entity", meta = (WorldContext = "WorldContextObject"))
	static void DestroyEntity(
		UObject* WorldContextObject,
		FSkeletonKey EntityKey);

	/**
	 * Destroy multiple entities.
	 *
	 * @param WorldContextObject World context
	 * @param EntityKeys Keys of entities to destroy
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Entity", meta = (WorldContext = "WorldContextObject"))
	static void DestroyEntities(
		UObject* WorldContextObject,
		const TArray<FSkeletonKey>& EntityKeys);

	// ═══════════════════════════════════════════════════════════════
	// QUERIES
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Check if an entity exists and is alive.
	 *
	 * @param WorldContextObject World context
	 * @param EntityKey Key to check
	 * @return True if entity exists and is not dead
	 */
	UFUNCTION(BlueprintPure, Category = "Flecs|Entity", meta = (WorldContext = "WorldContextObject"))
	static bool IsEntityAlive(
		UObject* WorldContextObject,
		FSkeletonKey EntityKey);

	/**
	 * Get the Flecs entity ID for a SkeletonKey.
	 *
	 * @param WorldContextObject World context
	 * @param EntityKey SkeletonKey to look up
	 * @return Flecs entity ID (0 if not found)
	 */
	UFUNCTION(BlueprintPure, Category = "Flecs|Entity", meta = (WorldContext = "WorldContextObject"))
	static int64 GetEntityId(
		UObject* WorldContextObject,
		FSkeletonKey EntityKey);

};
