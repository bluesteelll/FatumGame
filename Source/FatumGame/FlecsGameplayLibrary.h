// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Blueprint function library for Flecs ECS gameplay operations.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SkeletonTypes.h"
#include "EPhysicsLayer.h"
#include "FlecsGameplayLibrary.generated.h"

class UPrimaryDataAsset;
class UStaticMesh;
class UFlecsArtillerySubsystem;
class UFlecsProjectileDefinition;

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
	 * Creates: FItemData, FBarrageBody, FISMRender, FTagItem, FTagPickupable
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
	 * Creates: FHealthData, FBarrageBody, FISMRender, FTagDestructible
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
	 * Creates: FHealthData, FLootData, FBarrageBody, FISMRender, FTagDestructible, FTagHasLoot
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

	/** Get entity health data. Returns false if entity has no FHealthData. Artillery thread only. */
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
};
