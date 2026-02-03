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
class UFlecsConstrainedGroupDefinition;

/**
 * Blueprint-callable functions for Flecs ECS gameplay operations.
 *
 * Thread safety:
 * - Functions with WorldContext parameter are game-thread safe (operations enqueued to Artillery thread)
 * - Static functions taking UFlecsArtillerySubsystem* are Artillery-thread only (for collision handlers)
 */
UCLASS()
class UFlecsGameplayLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// SPAWN (game-thread safe)
	// Creates Barrage body + ISM render on game thread,
	// then enqueues Flecs entity creation to Artillery thread.
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
	 * Spawn a projectile from a FlecsProjectileDefinition data asset.
	 * This is the preferred method - configure projectile in editor, spawn at runtime.
	 *
	 * @param Definition The projectile data asset (configure in Content Browser)
	 * @param Location Spawn location (muzzle position)
	 * @param Direction Direction to fire (will be normalized)
	 * @param SpeedOverride Override speed from definition (0 = use definition)
	 */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Projectile", meta = (WorldContext = "WorldContextObject"))
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
	// ENTITY LIFECYCLE (game-thread safe, enqueued to Artillery thread)
	// ═══════════════════════════════════════════════════════════════

	/** Mark an entity as dead by its Barrage SkeletonKey. Cleaned up next tick. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Entity", meta = (WorldContext = "WorldContextObject"))
	static void KillEntityByBarrageKey(UObject* WorldContextObject, FSkeletonKey BarrageKey);

	// ═══════════════════════════════════════════════════════════════
	// DAMAGE & HEALING (game-thread safe, enqueued to Artillery thread)
	// ═══════════════════════════════════════════════════════════════

	/** Apply damage to an entity by its Barrage SkeletonKey. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Damage", meta = (WorldContext = "WorldContextObject"))
	static void ApplyDamageByBarrageKey(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Damage);

	/** Heal an entity by its Barrage SkeletonKey. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Damage", meta = (WorldContext = "WorldContextObject"))
	static void HealEntityByBarrageKey(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Amount);

	// ═══════════════════════════════════════════════════════════════
	// ITEM OPERATIONS (game-thread safe, enqueued to Artillery thread)
	// ═══════════════════════════════════════════════════════════════

	/** Set despawn timer on an item entity. -1 = never despawns. */
	UFUNCTION(BlueprintCallable, Category = "Flecs|Item", meta = (WorldContext = "WorldContextObject"))
	static void SetItemDespawnTimer(UObject* WorldContextObject, FSkeletonKey BarrageKey, float Timer);

	// ═══════════════════════════════════════════════════════════════
	// ARTILLERY THREAD API (C++ only, for collision handlers)
	// These operate directly on Flecs entities. NOT thread-safe
	// from game thread. Use from ArtilleryTick / collision handlers.
	// ═══════════════════════════════════════════════════════════════

	/** Apply damage directly on Artillery thread. Returns true if entity died. */
	static bool ApplyDamage_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey, float Damage);

	/** Heal directly on Artillery thread. */
	static void Heal_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey, float Amount);

	/** Check if an entity is alive. Artillery thread only. */
	static bool IsAlive_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey);

	/** Get entity health data. Returns false if entity has no FHealthInstance. Artillery thread only. */
	static bool GetHealth_ArtilleryThread(UFlecsArtillerySubsystem* Subsystem, FSkeletonKey BarrageKey,
		float& OutCurrentHP, float& OutMaxHP);

	// ═══════════════════════════════════════════════════════════════
	// QUERY (game-thread) - CROSS-THREAD READ
	// These read from Flecs which runs on Artillery thread (~120Hz).
	// Values may be stale by 1-2 frames. Safe for UI/cosmetics.
	// For critical gameplay decisions, use _ArtilleryThread variants
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
	// CONSTRAINTS (game-thread safe, enqueued to Artillery thread)
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
};
