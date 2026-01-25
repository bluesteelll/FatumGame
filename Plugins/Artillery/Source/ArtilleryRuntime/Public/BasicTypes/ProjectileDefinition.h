// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NiagaraSystem.h"
#include "NiagaraDataChannelPublic.h"
#include "EPhysicsLayer.h"
#include "ProjectileDefinition.generated.h"

/**
 * Blueprint-friendly DataAsset for defining projectile types.
 * Create in Content Browser: Right Click → Miscellaneous → Data Asset → ProjectileDefinition
 *
 * Supports both standard (sensor) and bouncing projectiles.
 */
UCLASS(BlueprintType)
class ARTILLERYRUNTIME_API UProjectileDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// IDENTIFICATION
	// ═══════════════════════════════════════════════════════════════

	/** Unique name for this projectile type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	FName ProjectileName = "DefaultProjectile";

	// ═══════════════════════════════════════════════════════════════
	// VISUALS
	// ═══════════════════════════════════════════════════════════════

	/** Static mesh for instanced rendering (required) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals")
	TObjectPtr<UStaticMesh> ProjectileMesh;

	/** Optional: Niagara Data Channel for GPU particle rendering */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals")
	TObjectPtr<UNiagaraDataChannelAsset> ParticleEffectDataChannel;

	/** Visual scale multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals", meta = (ClampMin = "0.1", ClampMax = "10"))
	float VisualScale = 1.0f;

	// ═══════════════════════════════════════════════════════════════
	// PHYSICS MODE
	// ═══════════════════════════════════════════════════════════════

	/** If true: bounces off surfaces. If false: destroyed on hit (sensor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	bool bIsBouncing = false;

	/** Collision layer for this projectile. Determines what it collides with.
	 *  - DEBRIS: Collides with NON_MOVING only (static environment)
	 *  - MOVING: Collides with NON_MOVING and other MOVING objects
	 *  - PROJECTILE: Standard projectile layer (sensor, destroyed on hit)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	EPhysicsLayer CollisionLayer = EPhysicsLayer::DEBRIS;

	// ═══════════════════════════════════════════════════════════════
	// BOUNCING SETTINGS (only used if bIsBouncing = true)
	// ═══════════════════════════════════════════════════════════════

	/** Bounce elasticity: 0 = no bounce, 0.8 = good, 1 = perfect elastic */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bouncing", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bIsBouncing"))
	float Restitution = 0.8f;

	/** Surface friction: 0 = ice, 0.2 = normal, 1 = sticky */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bouncing", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bIsBouncing"))
	float Friction = 0.2f;

	/** Gravity multiplier: 0 = laser, 0.3 = light, 1 = normal */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bouncing", meta = (ClampMin = "0", ClampMax = "3", EditCondition = "bIsBouncing"))
	float GravityFactor = 0.3f;

	/** Collision sphere radius (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bouncing", meta = (ClampMin = "1", ClampMax = "100", EditCondition = "bIsBouncing"))
	float CollisionRadius = 5.0f;

	// ═══════════════════════════════════════════════════════════════
	// LIFETIME
	// ═══════════════════════════════════════════════════════════════

	/** Lifetime in seconds (0 = use default 20s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lifetime", meta = (ClampMin = "0"))
	float LifetimeSeconds = 0.0f;

	/** Default muzzle velocity (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lifetime", meta = (ClampMin = "100"))
	float DefaultSpeed = 5000.0f;

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId("ProjectileDefinition", ProjectileName);
	}
};
