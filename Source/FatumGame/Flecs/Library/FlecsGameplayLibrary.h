// Blueprint function library for Flecs ECS gameplay operations.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SkeletonTypes.h"
#include "EPhysicsLayer.h"
#include "FlecsConstrainedGroupDefinition.h"
#include "FlecsGameplayLibrary.generated.h"

class UPrimaryDataAsset;
class UStaticMesh;
class UFlecsArtillerySubsystem;
class UFlecsProjectileDefinition;
class UFlecsEntityDefinition;
class UFlecsConstrainedGroupDefinition;

/**
 * Blueprint-callable functions for Flecs ECS gameplay operations.
 *
 * Thread safety:
 * - Functions with WorldContext parameter are game-thread safe (operations enqueued to simulation thread)
 * - Static functions taking UFlecsArtillerySubsystem* are simulation-thread only (for collision handlers)
 */
UCLASS()
class UFlecsGameplayLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// SPAWN (game-thread safe)
	// Creates Barrage body + ISM render on game thread,
	// then enqueues Flecs entity creation to simulation thread.
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Spawn a world item with physics, rendering, and Flecs entity.
	 * Creates: FItemStaticData + FItemInstance, FWorldItemInstance, FBarrageBody, FISMRender, FTagItem, FTagPickupable
	 */
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

	/**
	 * Spawn a destructible entity with health, physics, and rendering.
	 * Creates: FHealthStatic + FHealthInstance, FBarrageBody, FISMRender, FTagDestructible
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Spawn", meta = (WorldContext = "WorldContextObject"))
	static void SpawnDestructible(
		UObject* WorldContextObject,
		UStaticMesh* Mesh,
		FVector Location,
		float MaxHP = 100.f,
		EPhysicsLayer PhysicsLayer = EPhysicsLayer::MOVING
	);

	/**
	 * Spawn a destructible entity that drops loot on death.
	 * Creates: FHealthStatic + FHealthInstance, FLootStatic, FBarrageBody, FISMRender, FTagDestructible, FTagHasLoot
	 */
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

	/**
	 * Spawn a projectile from UFlecsEntityDefinition (RECOMMENDED).
	 * Uses prefab system for efficient memory usage.
	 * EntityDefinition must have: PhysicsProfile, RenderProfile, ProjectileProfile, DamageProfile (optional).
	 *
	 * @param Definition Entity definition with projectile profiles
	 * @param Location Spawn location (muzzle position)
	 * @param Direction Direction to fire (will be normalized)
	 * @param SpeedOverride Override speed from ProjectileProfile (0 = use profile)
	 * @param OwnerEntityId Flecs entity ID of the shooter (for friendly fire check)
	 * @return SkeletonKey of spawned projectile
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Projectile", meta = (WorldContext = "WorldContextObject"))
	static FSkeletonKey SpawnProjectileFromEntityDef(
		UObject* WorldContextObject,
		UFlecsEntityDefinition* Definition,
		FVector Location,
		FVector Direction,
		float SpeedOverride = 0.f,
		int64 OwnerEntityId = 0
	);

	/**
	 * DEPRECATED: Use SpawnProjectileFromEntityDef with UFlecsEntityDefinition instead.
	 * Spawn a projectile from old FlecsProjectileDefinition data asset.
	 */
	UE_DEPRECATED(5.7, "Use SpawnProjectileFromEntityDef with UFlecsEntityDefinition instead")
	UFUNCTION(BlueprintCallable, Category = "Flecs|Projectile", meta = (WorldContext = "WorldContextObject", DeprecatedFunction, DeprecationMessage = "Use SpawnProjectileFromEntityDef"))
	static FSkeletonKey SpawnProjectileFromDefinition(
		UObject* WorldContextObject,
		UFlecsProjectileDefinition* Definition,
		FVector Location,
		FVector Direction,
		float SpeedOverride = 0.f
	);

	/**
	 * Spawn a group of constrained entities from a definition asset.
	 * Creates all elements with physics bodies, Flecs entities, and constraints.
	 *
	 * @param Definition The group data asset (configure in Content Browser)
	 * @param Location World location for the group origin
	 * @param Rotation World rotation for the group
	 * @return Spawn result containing all element keys and constraint keys
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Spawn", meta = (WorldContext = "WorldContextObject"))
	static FFlecsGroupSpawnResult SpawnConstrainedGroup(
		UObject* WorldContextObject,
		UFlecsConstrainedGroupDefinition* Definition,
		FVector Location,
		FRotator Rotation = FRotator::ZeroRotator
	);

	/**
	 * Spawn a simple chain of linked boxes.
	 * Quick helper for common use case without needing a data asset.
	 *
	 * @param Mesh Mesh for each element
	 * @param StartLocation World location for the first element
	 * @param Direction Direction of the chain
	 * @param Count Number of elements in chain
	 * @param Spacing Distance between elements
	 * @param BreakForce Force that breaks links (0 = unbreakable)
	 * @return Spawn result
	 */
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

	/**
	 * Spawn a physics projectile as a Flecs entity (manual parameters).
	 * Use SpawnProjectileFromDefinition for data-driven approach.
	 *
	 * @param MaxBounces Max bounces before despawn. -1 = infinite (true bouncing projectile).
	 *                   0+ = will be destroyed after hitting N+1 targets/surfaces.
	 */
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

	// ═══════════════════════════════════════════════════════════════
	// ENTITY LIFECYCLE (game-thread safe, enqueued to simulation thread)
	// ═══════════════════════════════════════════════════════════════

	/** Mark an entity as dead by its Barrage SkeletonKey. Cleaned up next tick. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Entity", meta = (WorldContext = "WorldContextObject"))
	static void KillEntityByBarrageKey(UObject* WorldContextObject, FSkeletonKey BarrageKey);

	// ═══════════════════════════════════════════════════════════════
	// DAMAGE & HEALING (game-thread safe, enqueued to simulation thread)
	// ═══════════════════════════════════════════════════════════════

	/** Apply damage to an entity by its Barrage SkeletonKey. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Damage", meta = (WorldContext = "WorldContextObject"))
	static void ApplyDamageByBarrageKey(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Damage);

	/**
	 * Apply damage with damage type and options.
	 * @param BarrageKey Target entity key.
	 * @param Damage Base damage amount.
	 * @param DamageType Damage type tag for resistances.
	 * @param bIgnoreArmor If true, bypasses armor calculation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Damage", meta = (WorldContext = "WorldContextObject"))
	static void ApplyDamageWithType(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Damage,
	                                FGameplayTag DamageType, bool bIgnoreArmor = false);

	/** Heal an entity by its Barrage SkeletonKey. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Damage", meta = (WorldContext = "WorldContextObject"))
	static void HealEntityByBarrageKey(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Amount);

	// ═══════════════════════════════════════════════════════════════
	// ITEM OPERATIONS (game-thread safe, enqueued to simulation thread)
	// ═══════════════════════════════════════════════════════════════

	/** Set despawn timer on an item entity. -1 = never despawns. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Item", meta = (WorldContext = "WorldContextObject"))
	static void SetItemDespawnTimer(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Timer);

	// ═══════════════════════════════════════════════════════════════
	// SIMULATION THREAD API (C++ only, for collision handlers)
	// These operate directly on Flecs entities. NOT thread-safe
	// from game thread. Use from SimTick / collision handlers.
	// ═══════════════════════════════════════════════════════════════

	/** Apply damage directly on simulation thread. Returns true if entity died. */
	static bool ApplyDamage_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey, float Damage);

	/** Heal directly on simulation thread. */
	static void Heal_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey, float Amount);

	/** Check if an entity is alive. simulation thread only. */
	static bool IsAlive_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey);

	/** Get entity health data. Returns false if entity has no FHealthInstance. simulation thread only. */
	static bool GetHealth_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey,
		float& OutCurrentHP, float& OutMaxHP);

	// ═══════════════════════════════════════════════════════════════
	// QUERY (game-thread) - CROSS-THREAD READ
	// These read from Flecs which runs on simulation thread (~120Hz).
	// Values may be stale by 1-2 frames. Safe for UI/cosmetics.
	// For critical gameplay decisions, use _SimThread variants
	// via EnqueueCommand().
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Get current health for an entity. Returns -1 if entity has no health.
	 * WARNING: Cross-thread read - value may be stale by 1-2 frames.
	 */
	UFUNCTION(BlueprintPure, Category = "Flecs|Query", meta = (WorldContext = "WorldContextObject"))
	static float GetEntityHealth(UObject* WorldContextObject, FSkeletonKey BarrageKey);

	/**
	 * Get max health for an entity. Returns -1 if entity has no health.
	 * WARNING: Cross-thread read - value may be stale by 1-2 frames.
	 */
	UFUNCTION(BlueprintPure, Category = "Flecs|Query", meta = (WorldContext = "WorldContextObject"))
	static float GetEntityMaxHealth(UObject* WorldContextObject, FSkeletonKey BarrageKey);

	/**
	 * Check if entity is alive (has health > 0).
	 * WARNING: Cross-thread read - value may be stale by 1-2 frames.
	 */
	UFUNCTION(BlueprintPure, Category = "Flecs|Query", meta = (WorldContext = "WorldContextObject"))
	static bool IsEntityAlive(UObject* WorldContextObject, FSkeletonKey BarrageKey);

	// ═══════════════════════════════════════════════════════════════
	// CONSTRAINTS (game-thread safe, enqueued to simulation thread)
	// Create breakable physics connections between Flecs entities.
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Create a fixed (welded) constraint between two entities.
	 * Bodies will move as if welded together until the constraint breaks.
	 *
	 * @param Entity1Key SkeletonKey of first entity (must have FBarrageBody)
	 * @param Entity2Key SkeletonKey of second entity (must have FBarrageBody)
	 * @param BreakForce Force in Newtons that will break the constraint (0 = unbreakable)
	 * @param BreakTorque Torque in Nm that will break the constraint (0 = unbreakable)
	 * @return Constraint key for later management (0 = failed)
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Constraints", meta = (WorldContext = "WorldContextObject"))
	static int64 CreateFixedConstraint(
		UObject* WorldContextObject,
		FSkeletonKey Entity1Key,
		FSkeletonKey Entity2Key,
		float BreakForce = 0.f,
		float BreakTorque = 0.f
	);

	/**
	 * Create a hinge (rotating) constraint between two entities.
	 * Bodies can rotate around the specified axis.
	 *
	 * @param Entity1Key SkeletonKey of first entity
	 * @param Entity2Key SkeletonKey of second entity
	 * @param WorldAnchor World position of the hinge point
	 * @param HingeAxis Axis of rotation (world space, will be normalized)
	 * @param BreakForce Force in Newtons that will break the constraint (0 = unbreakable)
	 * @return Constraint key for later management (0 = failed)
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Constraints", meta = (WorldContext = "WorldContextObject"))
	static int64 CreateHingeConstraint(
		UObject* WorldContextObject,
		FSkeletonKey Entity1Key,
		FSkeletonKey Entity2Key,
		FVector WorldAnchor,
		FVector HingeAxis,
		float BreakForce = 0.f
	);

	/**
	 * Create a distance (rope/spring) constraint between two entities.
	 * Maintains distance between anchor points.
	 *
	 * @param Entity1Key SkeletonKey of first entity
	 * @param Entity2Key SkeletonKey of second entity
	 * @param MinDistance Minimum distance in cm (0 = no minimum, acts like rope)
	 * @param MaxDistance Maximum distance in cm (0 = auto-detect from current position)
	 * @param BreakForce Force that will break the rope (0 = unbreakable)
	 * @param SpringFrequency Spring stiffness in Hz (0 = rigid, 1-5 = soft, 10+ = stiff)
	 * @param SpringDamping Damping ratio 0-1 (0 = bouncy, 1 = no bounce)
	 * @param bLockRotation Lock relative rotation (bodies can't rotate independently, like a telescoping rod)
	 * @return Constraint key for later management (0 = failed)
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Constraints", meta = (WorldContext = "WorldContextObject"))
	static int64 CreateDistanceConstraint(
		UObject* WorldContextObject,
		FSkeletonKey Entity1Key,
		FSkeletonKey Entity2Key,
		float MinDistance = 0.f,
		float MaxDistance = 0.f,
		float BreakForce = 0.f,
		float SpringFrequency = 0.f,
		float SpringDamping = 0.5f,
		bool bLockRotation = false
	);

	/**
	 * Create a point (ball joint) constraint between two entities.
	 * Bodies can rotate freely around the connection point.
	 *
	 * @param Entity1Key SkeletonKey of first entity
	 * @param Entity2Key SkeletonKey of second entity
	 * @param BreakForce Force in Newtons that will break the constraint (0 = unbreakable)
	 * @param BreakTorque Torque in Nm that will break the constraint (0 = unbreakable)
	 * @return Constraint key for later management (0 = failed)
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Constraints", meta = (WorldContext = "WorldContextObject"))
	static int64 CreatePointConstraint(
		UObject* WorldContextObject,
		FSkeletonKey Entity1Key,
		FSkeletonKey Entity2Key,
		float BreakForce = 0.f,
		float BreakTorque = 0.f
	);

	/**
	 * Remove a constraint by its key.
	 *
	 * @param ConstraintKey The constraint to remove
	 * @return True if the constraint was found and removed
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Constraints", meta = (WorldContext = "WorldContextObject"))
	static bool RemoveConstraint(UObject* WorldContextObject, int64 ConstraintKey);

	/**
	 * Remove all constraints from an entity.
	 *
	 * @param EntityKey SkeletonKey of the entity
	 * @return Number of constraints removed
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Constraints", meta = (WorldContext = "WorldContextObject"))
	static int32 RemoveAllConstraintsFromEntity(UObject* WorldContextObject, FSkeletonKey EntityKey);

	/**
	 * Check if a constraint is still active (not broken).
	 *
	 * @param ConstraintKey The constraint to check
	 * @return True if the constraint exists and is active
	 */
	UFUNCTION(BlueprintPure, Category = "Flecs|Constraints", meta = (WorldContext = "WorldContextObject"))
	static bool IsConstraintActive(UObject* WorldContextObject, int64 ConstraintKey);

	/**
	 * Get the current stress ratio on a constraint (0 = no stress, 1+ = should break).
	 * Useful for UI feedback before breaking.
	 *
	 * @param ConstraintKey The constraint to query
	 * @param OutStressRatio Receives the stress ratio (force/breakForce)
	 * @return True if the constraint exists
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Constraints", meta = (WorldContext = "WorldContextObject"))
	static bool GetConstraintStressRatio(UObject* WorldContextObject, int64 ConstraintKey, float& OutStressRatio);

	// ═══════════════════════════════════════════════════════════════
	// WEAPON CONTROL (game-thread safe, enqueued to simulation thread)
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Start firing a weapon (hold trigger).
	 * For automatic weapons, will continue firing while held.
	 * For semi-auto, fires once per call (must call StopFiring between shots).
	 *
	 * @param WeaponEntityId Flecs entity ID of the weapon
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Weapon", meta = (WorldContext = "WorldContextObject"))
	static void StartFiring(UObject* WorldContextObject, int64 WeaponEntityId);

	/**
	 * Stop firing a weapon (release trigger).
	 * Required for semi-auto weapons to fire again.
	 *
	 * @param WeaponEntityId Flecs entity ID of the weapon
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Weapon", meta = (WorldContext = "WorldContextObject"))
	static void StopFiring(UObject* WorldContextObject, int64 WeaponEntityId);

	/**
	 * Request weapon reload.
	 * Will reload if: not already reloading, magazine not full, has reserve ammo.
	 *
	 * @param WeaponEntityId Flecs entity ID of the weapon
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Weapon", meta = (WorldContext = "WorldContextObject"))
	static void ReloadWeapon(UObject* WorldContextObject, int64 WeaponEntityId);

	/**
	 * Set aim direction for a character.
	 * Used by WeaponFireSystem to determine projectile direction.
	 *
	 * @param CharacterEntityId Flecs entity ID of the character
	 * @param Direction Aim direction (will be normalized)
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Weapon", meta = (WorldContext = "WorldContextObject"))
	static void SetAimDirection(UObject* WorldContextObject, int64 CharacterEntityId, FVector Direction, FVector MuzzleOffset = FVector::ZeroVector, FVector CharacterPosition = FVector::ZeroVector);

	/**
	 * Get current ammo in weapon magazine.
	 * WARNING: Cross-thread read - value may be stale by 1-2 frames.
	 *
	 * @param WeaponEntityId Flecs entity ID of the weapon
	 * @return Current ammo count, -1 if not a weapon
	 */
	UFUNCTION(BlueprintPure, Category = "Flecs|Weapon", meta = (WorldContext = "WorldContextObject"))
	static int32 GetWeaponAmmo(UObject* WorldContextObject, int64 WeaponEntityId);

	/**
	 * Get weapon ammo info (current, magazine size, reserve).
	 * WARNING: Cross-thread read - values may be stale by 1-2 frames.
	 *
	 * @param WeaponEntityId Flecs entity ID of the weapon
	 * @param OutCurrentAmmo Current ammo in magazine
	 * @param OutMagazineSize Magazine capacity
	 * @param OutReserveAmmo Reserve ammo
	 * @return True if weapon found
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Weapon", meta = (WorldContext = "WorldContextObject"))
	static bool GetWeaponAmmoInfo(UObject* WorldContextObject, int64 WeaponEntityId,
		int32& OutCurrentAmmo, int32& OutMagazineSize, int32& OutReserveAmmo);

	/**
	 * Check if weapon is currently reloading.
	 * WARNING: Cross-thread read - value may be stale by 1-2 frames.
	 *
	 * @param WeaponEntityId Flecs entity ID of the weapon
	 * @return True if reloading
	 */
	UFUNCTION(BlueprintPure, Category = "Flecs|Weapon", meta = (WorldContext = "WorldContextObject"))
	static bool IsWeaponReloading(UObject* WorldContextObject, int64 WeaponEntityId);
};
