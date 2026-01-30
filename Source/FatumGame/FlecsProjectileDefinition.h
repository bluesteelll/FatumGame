// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Data Asset for Flecs-based projectiles.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsProjectileDefinition.generated.h"

class UStaticMesh;
class UMaterialInterface;

/**
 * Data Asset defining a Flecs projectile type.
 * Create in Content Browser: Right Click -> Miscellaneous -> Data Asset -> FlecsProjectileDefinition
 *
 * Features:
 * - Physics-based projectiles (bouncing or sensor)
 * - Configurable damage, speed, gravity
 * - Auto-despawn lifetime
 * - ISM rendering for performance
 */
UCLASS(BlueprintType)
class FATUMGAME_API UFlecsProjectileDefinition : public UPrimaryDataAsset
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

	/** Static mesh for rendering */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals")
	TObjectPtr<UStaticMesh> Mesh;

	/** Optional material override */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals")
	TObjectPtr<UMaterialInterface> Material;

	/** Visual scale multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals", meta = (ClampMin = "0.1", ClampMax = "10"))
	float VisualScale = 1.0f;

	// ═══════════════════════════════════════════════════════════════
	// PHYSICS
	// ═══════════════════════════════════════════════════════════════

	/** Collision sphere radius (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (ClampMin = "1", ClampMax = "100"))
	float CollisionRadius = 5.0f;

	/** If true: bounces off surfaces. If false: destroyed on first hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	bool bIsBouncing = true;

	/** Bounce elasticity: 0 = no bounce, 0.8 = good, 1 = perfect elastic */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bIsBouncing"))
	float Restitution = 0.8f;

	/** Surface friction: 0 = ice, 0.2 = normal, 1 = sticky */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bIsBouncing"))
	float Friction = 0.2f;

	/** Gravity multiplier: 0 = laser, 0.3 = light, 1 = normal */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (ClampMin = "0", ClampMax = "3"))
	float GravityFactor = 0.3f;

	// ═══════════════════════════════════════════════════════════════
	// MOVEMENT
	// ═══════════════════════════════════════════════════════════════

	/** Default muzzle velocity (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "100"))
	float DefaultSpeed = 5000.0f;

	// ═══════════════════════════════════════════════════════════════
	// DAMAGE
	// ═══════════════════════════════════════════════════════════════

	/** Base damage dealt on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0"))
	float Damage = 25.0f;

	/** Is this an area damage projectile? (explosion) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	bool bAreaDamage = false;

	/** Explosion radius if bAreaDamage = true */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0", EditCondition = "bAreaDamage"))
	float AreaRadius = 200.0f;

	// ═══════════════════════════════════════════════════════════════
	// LIFETIME
	// ═══════════════════════════════════════════════════════════════

	/** Lifetime in seconds before auto-despawn */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lifetime", meta = (ClampMin = "0.1"))
	float LifetimeSeconds = 10.0f;

	/** Max bounces before despawn (-1 = infinite) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lifetime", meta = (EditCondition = "bIsBouncing"))
	int32 MaxBounces = -1;

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId("FlecsProjectileDefinition", ProjectileName);
	}
};
