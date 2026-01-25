// Full Blueprint integration for Barrage physics system
// Allows complete level design and gameplay scripting through Blueprints

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SkeletonTypes.h"
#include "EPhysicsLayer.h"
#include "BarrageBlueprintLibrary.generated.h"

/**
 * Complete Blueprint API for Barrage Physics (Jolt)
 * Use this library to create, manipulate, and query physics objects from Blueprints
 */
UCLASS(meta=(ScriptName="BarragePhysicsLibrary"))
class ARTILLERYRUNTIME_API UBarrageBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// CREATION - Create physics bodies
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Create a box physics body
	 * @param Location World position
	 * @param SizeX Box width (full size, not half!)
	 * @param SizeY Box length (full size, not half!)
	 * @param SizeZ Box height (full size, not half!)
	 * @param Layer Physics layer (NON_MOVING for static, MOVING for dynamic)
	 * @param bIsMovable Can this object move?
	 * @param bIsSensor Trigger without physics response?
	 * @return Entity key for the created body
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics", meta = (WorldContext = "WorldContextObject"))
	static FSkeletonKey CreateBoxBody(
		UObject* WorldContextObject,
		FVector Location,
		float SizeX = 100.0f,
		float SizeY = 100.0f,
		float SizeZ = 100.0f,
		EPhysicsLayer Layer = EPhysicsLayer::MOVING,
		bool bIsMovable = true,
		bool bIsSensor = false
	);

	/**
	 * Create a sphere physics body
	 * @param Location World position
	 * @param Radius Sphere radius
	 * @param Layer Physics layer
	 * @param bIsMovable Can this object move?
	 * @param bIsSensor Trigger without physics response?
	 * @return Entity key for the created body
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics", meta = (WorldContext = "WorldContextObject"))
	static FSkeletonKey CreateSphereBody(
		UObject* WorldContextObject,
		FVector Location,
		float Radius = 50.0f,
		EPhysicsLayer Layer = EPhysicsLayer::MOVING,
		bool bIsMovable = true,
		bool bIsSensor = false
	);

	/**
	 * Create a capsule physics body
	 * @param Location World position
	 * @param Radius Capsule radius
	 * @param HalfHeight Half height of the cylindrical part
	 * @param Layer Physics layer
	 * @param bIsMovable Can this object move?
	 * @param bIsSensor Trigger without physics response?
	 * @return Entity key for the created body
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics", meta = (WorldContext = "WorldContextObject"))
	static FSkeletonKey CreateCapsuleBody(
		UObject* WorldContextObject,
		FVector Location,
		float Radius = 50.0f,
		float HalfHeight = 100.0f,
		EPhysicsLayer Layer = EPhysicsLayer::MOVING,
		bool bIsMovable = true,
		bool bIsSensor = false
	);

	/**
	 * Create a large static floor box (quick way to add ground collision)
	 * @param Location Center of the floor
	 * @param Width Floor width (X axis)
	 * @param Length Floor length (Y axis)
	 * @param Thickness Floor thickness (Z axis)
	 * @return Entity key for the floor
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Helpers", meta = (WorldContext = "WorldContextObject"))
	static FSkeletonKey CreateFloorBox(
		UObject* WorldContextObject,
		FVector Location = FVector(0, 0, -50),
		float Width = 20000.0f,
		float Length = 20000.0f,
		float Thickness = 100.0f
	);

	// ═══════════════════════════════════════════════════════════════
	// BOUNCING PROJECTILES - Optimized system for ricocheting bullets
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Create a bouncing sphere projectile (physics only, no visual)
	 * For rendering: register with NDC using RegisterProjectileForNDC, then use Niagara with NDC Reader
	 *
	 * @param Location Spawn position
	 * @param Radius Projectile radius (keep small, 2-10 cm recommended)
	 * @param InitialVelocity Direction and speed of the projectile
	 * @param Restitution Bounce factor: 0.0 = no bounce, 0.8 = good bounce, 1.0 = perfect elastic
	 * @param Friction Surface friction: 0.0 = frictionless, 0.2 = recommended for bullets
	 * @param GravityFactor Gravity multiplier: 0 = no gravity (laser), 1 = normal, 2 = heavy
	 * @return Entity key for the projectile
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Projectiles", meta = (WorldContext = "WorldContextObject"))
	static FSkeletonKey CreateBouncingProjectile(
		UObject* WorldContextObject,
		FVector Location,
		float Radius = 5.0f,
		FVector InitialVelocity = FVector(5000, 0, 0),
		float Restitution = 0.8f,
		float Friction = 0.2f,
		float GravityFactor = 0.3f
	);

	/**
	 * Register a projectile for NDC-based Niagara rendering
	 * Call this after CreateBouncingProjectile to enable GPU-accelerated visualization
	 *
	 * @param ProjectileKey The projectile entity key
	 * @param NDCAssetName Name identifier for grouping (projectiles with same name share NDC channel)
	 * @param NDCAsset Optional: Niagara Data Channel asset for rendering. If null, must be set up separately
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Projectiles", meta = (WorldContext = "WorldContextObject"))
	static void RegisterProjectileForNDC(
		UObject* WorldContextObject,
		FSkeletonKey ProjectileKey,
		FName NDCAssetName = "BouncingBullets"
	);

	/**
	 * Spawn multiple bouncing projectiles in a spread pattern (shotgun effect)
	 * All projectiles are registered for NDC rendering automatically
	 *
	 * @param Location Spawn position
	 * @param Direction Base direction for all projectiles
	 * @param Speed Projectile speed
	 * @param Count Number of projectiles to spawn
	 * @param SpreadAngle Cone spread angle in degrees
	 * @param Radius Per-projectile radius
	 * @param Restitution Bounce factor
	 * @param NDCAssetName NDC group name for rendering
	 * @param OutProjectileKeys Output array of created projectile keys
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Projectiles", meta = (WorldContext = "WorldContextObject"))
	static void SpawnBouncingProjectileSpread(
		UObject* WorldContextObject,
		FVector Location,
		FVector Direction,
		float Speed,
		int32 Count,
		float SpreadAngle,
		float Radius,
		float Restitution,
		FName NDCAssetName,
		TArray<FSkeletonKey>& OutProjectileKeys
	);

	/**
	 * Load complex static mesh collision (for detailed geometry)
	 * @param StaticMeshComponent The mesh component to load collision from
	 * @param Transform World transform
	 * @return Entity key for the created body
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics", meta = (WorldContext = "WorldContextObject"))
	static FSkeletonKey LoadComplexMeshCollision(
		UObject* WorldContextObject,
		UStaticMeshComponent* StaticMeshComponent,
		FTransform Transform
	);

	// ═══════════════════════════════════════════════════════════════
	// VELOCITY & FORCES - Control motion
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Set linear velocity of a physics body
	 * @param EntityKey The entity to modify
	 * @param Velocity New velocity vector
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Velocity")
	static bool SetVelocity(FSkeletonKey EntityKey, FVector Velocity);

	/**
	 * Get current velocity of a physics body
	 * @param EntityKey The entity to query
	 * @param Velocity Output velocity
	 * @return True if entity exists
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Velocity", meta = (ExpandBoolAsExecs = "ReturnValue"))
	static bool GetVelocity(FSkeletonKey EntityKey, FVector& Velocity);

	/**
	 * Apply force to a physics body
	 * @param EntityKey The entity to apply force to
	 * @param Force Force vector to apply
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Forces")
	static bool ApplyForce(FSkeletonKey EntityKey, FVector Force);

	/**
	 * Apply impulse (instant velocity change) to a physics body
	 * @param EntityKey The entity to apply impulse to
	 * @param Impulse Impulse vector
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Forces")
	static bool ApplyImpulse(FSkeletonKey EntityKey, FVector Impulse);

	/**
	 * Apply torque (rotational force) to a physics body
	 * @param EntityKey The entity to apply torque to
	 * @param Torque Torque vector
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Forces")
	static bool ApplyTorque(FSkeletonKey EntityKey, FVector Torque);

	/**
	 * Set maximum speed limit for a physics body
	 * @param EntityKey The entity to limit
	 * @param MaxSpeed Maximum speed (0 = no limit)
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Velocity")
	static bool SetSpeedLimit(FSkeletonKey EntityKey, float MaxSpeed);

	// ═══════════════════════════════════════════════════════════════
	// POSITION & ROTATION - Transform manipulation
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Set position of a physics body (teleport)
	 * @param EntityKey The entity to move
	 * @param NewLocation New world position
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Transform")
	static bool SetPosition(FSkeletonKey EntityKey, FVector NewLocation);

	/**
	 * Get current position of a physics body
	 * @param EntityKey The entity to query
	 * @param Location Output location
	 * @return True if entity exists
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Transform", meta = (ExpandBoolAsExecs = "ReturnValue"))
	static bool GetPosition(FSkeletonKey EntityKey, FVector& Location);

	/**
	 * Set rotation of a physics body
	 * @param EntityKey The entity to rotate
	 * @param NewRotation New rotation
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Transform")
	static bool SetRotation(FSkeletonKey EntityKey, FRotator NewRotation);

	/**
	 * Get current rotation of a physics body
	 * @param EntityKey The entity to query
	 * @param Rotation Output rotation
	 * @return True if entity exists
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Transform", meta = (ExpandBoolAsExecs = "ReturnValue"))
	static bool GetRotation(FSkeletonKey EntityKey, FRotator& Rotation);

	/**
	 * Get current transform of a physics body
	 * @param EntityKey The entity to query
	 * @param Transform Output transform
	 * @return True if entity exists
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Transform", meta = (ExpandBoolAsExecs = "ReturnValue"))
	static bool GetTransform(FSkeletonKey EntityKey, FTransform& Transform);

	// ═══════════════════════════════════════════════════════════════
	// GRAVITY & PHYSICS PROPERTIES
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Set gravity factor for a physics body
	 * @param EntityKey The entity to modify
	 * @param GravityFactor Multiplier (0 = no gravity, 1 = normal, 2 = double)
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Properties")
	static bool SetGravityFactor(FSkeletonKey EntityKey, float GravityFactor);

	/**
	 * Set custom gravity direction for a character
	 * @param EntityKey The character entity
	 * @param CustomGravity Custom gravity vector
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Properties")
	static bool SetCharacterGravity(FSkeletonKey EntityKey, FVector CustomGravity);

	// ═══════════════════════════════════════════════════════════════
	// RAYCASTING & QUERIES
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Cast a ray in the physics world
	 * @param Start Ray start position
	 * @param End Ray end position
	 * @param Layer Physics layer to query
	 * @param HitResult Output hit information
	 * @param IgnoreEntity Optional entity to ignore
	 * @return True if hit something
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Queries", meta = (WorldContext = "WorldContextObject", ExpandBoolAsExecs = "ReturnValue", AutoCreateRefTerm = "IgnoreEntity"))
	static bool Raycast(
		UObject* WorldContextObject,
		FVector Start,
		FVector End,
		EPhysicsLayer Layer,
		FHitResult& HitResult,
		const FSkeletonKey& IgnoreEntity
	);

	/**
	 * Cast a sphere along a ray
	 * @param Start Start position
	 * @param End End position
	 * @param Radius Sphere radius
	 * @param Layer Physics layer to query
	 * @param HitResult Output hit information
	 * @param IgnoreEntity Optional entity to ignore
	 * @return True if hit something
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Queries", meta = (WorldContext = "WorldContextObject", ExpandBoolAsExecs = "ReturnValue", AutoCreateRefTerm = "IgnoreEntity"))
	static bool SphereCast(
		UObject* WorldContextObject,
		FVector Start,
		FVector End,
		float Radius,
		EPhysicsLayer Layer,
		FHitResult& HitResult,
		const FSkeletonKey& IgnoreEntity
	);

	/**
	 * Find all entities in a sphere
	 * @param Location Center of sphere
	 * @param Radius Search radius
	 * @param Layer Physics layer to query
	 * @param FoundEntities Output array of found entity keys
	 * @return Number of entities found
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Queries", meta = (WorldContext = "WorldContextObject"))
	static int32 SphereSearch(
		UObject* WorldContextObject,
		FVector Location,
		float Radius,
		EPhysicsLayer Layer,
		TArray<FSkeletonKey>& FoundEntities
	);

	// ═══════════════════════════════════════════════════════════════
	// DELETION & LIFECYCLE
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Destroy a physics entity (safe tombstoning)
	 * @param EntityKey The entity to destroy
	 * @return True if entity was found and destroyed
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Physics|Lifecycle")
	static bool DestroyEntity(FSkeletonKey EntityKey);

	/**
	 * Check if an entity is still valid (not destroyed)
	 * @param EntityKey The entity to check
	 * @return True if entity exists and is valid
	 */
	UFUNCTION(BlueprintPure, Category = "Barrage|Physics|Lifecycle")
	static bool IsEntityValid(FSkeletonKey EntityKey);

	// ═══════════════════════════════════════════════════════════════
	// DEBUGGING & DIAGNOSTICS
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Get total number of active Barrage physics bodies
	 * @return Body count
	 */
	UFUNCTION(BlueprintPure, Category = "Barrage|Debug", meta = (WorldContext = "WorldContextObject"))
	static int32 GetTotalBodyCount(UObject* WorldContextObject);

	/**
	 * Draw debug box for a physics body (for one frame)
	 * @param EntityKey The entity to visualize
	 * @param Color Debug color
	 * @param Thickness Line thickness
	 */
	UFUNCTION(BlueprintCallable, Category = "Barrage|Debug", meta = (WorldContext = "WorldContextObject"))
	static void DrawDebugPhysicsBody(
		UObject* WorldContextObject,
		FSkeletonKey EntityKey,
		FLinearColor Color = FLinearColor::Green,
		float Thickness = 2.0f
	);
};
