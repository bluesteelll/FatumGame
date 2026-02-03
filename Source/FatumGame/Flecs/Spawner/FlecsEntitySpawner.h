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

	/** Owner entity key (for containers, projectiles) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overrides")
	FSkeletonKey OwnerKey;

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

	/** Set owner */
	FEntitySpawnRequest& WithOwner(FSkeletonKey Owner)
	{
		OwnerKey = Owner;
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

	// ═══════════════════════════════════════════════════════════════
	// ITEM OPERATIONS (convenience wrappers)
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Add item to a container entity using EntityDefinition (preferred).
	 * Uses Flecs prefabs for shared static data - EntityDefinition is stored
	 * in prefab and can be retrieved later for world spawning.
	 *
	 * @param WorldContextObject World context
	 * @param ContainerKey Container entity
	 * @param EntityDefinition Entity definition with ItemDefinition profile
	 * @param Count How many to add
	 * @param OutActuallyAdded How many were actually added
	 * @return True if any items were added
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Items", meta = (WorldContext = "WorldContextObject"))
	static bool AddItemToContainerFromDefinition(
		UObject* WorldContextObject,
		FSkeletonKey ContainerKey,
		UFlecsEntityDefinition* EntityDefinition,
		int32 Count,
		int32& OutActuallyAdded);

	/**
	 * Add item to a container entity (legacy - uses ItemDefinition only).
	 * NOTE: Prefer AddItemToContainerFromDefinition which stores EntityDefinition
	 * reference for later retrieval (e.g., when dropping item to world).
	 *
	 * @param WorldContextObject World context
	 * @param ContainerKey Container entity
	 * @param ItemDefinition Item type to add
	 * @param Count How many to add
	 * @param OutActuallyAdded How many were actually added
	 * @return True if any items were added
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Items", meta = (WorldContext = "WorldContextObject"))
	static bool AddItemToContainer(
		UObject* WorldContextObject,
		FSkeletonKey ContainerKey,
		UFlecsItemDefinition* ItemDefinition,
		int32 Count,
		int32& OutActuallyAdded);

	/**
	 * Remove item from a container entity.
	 *
	 * @param WorldContextObject World context
	 * @param ContainerKey Container entity
	 * @param ItemEntityId Item to remove
	 * @param Count How many to remove (-1 = all)
	 * @return True if removed
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Items", meta = (WorldContext = "WorldContextObject"))
	static bool RemoveItemFromContainer(
		UObject* WorldContextObject,
		FSkeletonKey ContainerKey,
		int64 ItemEntityId,
		int32 Count = -1);

	/**
	 * Remove ALL items from a container entity.
	 *
	 * @param WorldContextObject World context
	 * @param ContainerKey Container entity
	 * @return Number of items removed
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Items", meta = (WorldContext = "WorldContextObject"))
	static int32 RemoveAllItemsFromContainer(
		UObject* WorldContextObject,
		FSkeletonKey ContainerKey);

	/**
	 * Get number of items in a container.
	 *
	 * @param WorldContextObject World context
	 * @param ContainerKey Container entity
	 * @return Number of items (-1 if not a container)
	 */
	UFUNCTION(BlueprintPure, Category = "Flecs|Items", meta = (WorldContext = "WorldContextObject"))
	static int32 GetContainerItemCount(
		UObject* WorldContextObject,
		FSkeletonKey ContainerKey);

	/**
	 * Pickup a world item into a container.
	 *
	 * @param WorldContextObject World context
	 * @param WorldItemKey World item to pick up
	 * @param ContainerKey Target container
	 * @param OutPickedUp How many were picked up
	 * @return True if any items were picked up
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Items", meta = (WorldContext = "WorldContextObject"))
	static bool PickupItem(
		UObject* WorldContextObject,
		FSkeletonKey WorldItemKey,
		FSkeletonKey ContainerKey,
		int32& OutPickedUp);

	/**
	 * Drop item from container into world.
	 *
	 * @param WorldContextObject World context
	 * @param ContainerKey Container holding the item
	 * @param ItemEntityId Item to drop
	 * @param DropLocation World location to drop at
	 * @param Count How many to drop (-1 = all)
	 * @return SkeletonKey of the world item
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Items", meta = (WorldContext = "WorldContextObject"))
	static FSkeletonKey DropItem(
		UObject* WorldContextObject,
		FSkeletonKey ContainerKey,
		int64 ItemEntityId,
		FVector DropLocation,
		int32 Count = -1);

	// ═══════════════════════════════════════════════════════════════
	// HEALTH OPERATIONS (convenience wrappers)
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Apply damage to an entity.
	 *
	 * @param WorldContextObject World context
	 * @param TargetKey Entity to damage
	 * @param Damage Amount of damage
	 * @return True if damage was applied
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Health", meta = (WorldContext = "WorldContextObject"))
	static bool ApplyDamage(
		UObject* WorldContextObject,
		FSkeletonKey TargetKey,
		float Damage);

	/**
	 * Heal an entity.
	 *
	 * @param WorldContextObject World context
	 * @param TargetKey Entity to heal
	 * @param Amount Amount to heal
	 * @return True if healed
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Health", meta = (WorldContext = "WorldContextObject"))
	static bool Heal(
		UObject* WorldContextObject,
		FSkeletonKey TargetKey,
		float Amount);

	/**
	 * Kill an entity instantly.
	 *
	 * @param WorldContextObject World context
	 * @param TargetKey Entity to kill
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Health", meta = (WorldContext = "WorldContextObject"))
	static void Kill(
		UObject* WorldContextObject,
		FSkeletonKey TargetKey);

	/**
	 * Get current health of an entity.
	 *
	 * @param WorldContextObject World context
	 * @param EntityKey Entity to query
	 * @return Current health (0 if no health component)
	 */
	UFUNCTION(BlueprintPure, Category = "Flecs|Health", meta = (WorldContext = "WorldContextObject"))
	static float GetHealth(
		UObject* WorldContextObject,
		FSkeletonKey EntityKey);

	/**
	 * Get max health of an entity.
	 *
	 * @param WorldContextObject World context
	 * @param EntityKey Entity to query
	 * @return Max health (0 if no health component)
	 */
	UFUNCTION(BlueprintPure, Category = "Flecs|Health", meta = (WorldContext = "WorldContextObject"))
	static float GetMaxHealth(
		UObject* WorldContextObject,
		FSkeletonKey EntityKey);
};
