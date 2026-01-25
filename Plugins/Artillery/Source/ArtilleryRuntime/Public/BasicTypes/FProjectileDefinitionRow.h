#pragma once

#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "Engine/DataTable.h"

#include "FProjectileDefinitionRow.generated.h"

USTRUCT(BlueprintType)
struct FProjectileDefinitionRow : public FTableRowBase
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=ProjectileDefinition)
	FString ProjectileDefinitionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ProjectileDefinition)
	TObjectPtr<UStaticMesh> ProjectileMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ProjectileDefinition)
	TObjectPtr<UNiagaraDataChannelAsset> ParticleEffectDataChannel;

	// ═══════════════════════════════════════════════════════════════
	// BOUNCING PROJECTILE SETTINGS
	// Set bIsBouncing=true to create projectiles that bounce off surfaces
	// ═══════════════════════════════════════════════════════════════

	/** If true, projectile bounces off surfaces instead of being destroyed on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bouncing")
	bool bIsBouncing = false;

	/** Bounce elasticity: 0 = no bounce, 0.8 = good bounce, 1 = perfect elastic */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bouncing", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bIsBouncing"))
	float Restitution = 0.8f;

	/** Surface friction: 0 = ice, 0.2 = normal bullet, 1 = sticky */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bouncing", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bIsBouncing"))
	float Friction = 0.2f;

	/** Gravity multiplier: 0 = laser, 0.3 = light, 1 = normal */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bouncing", meta = (ClampMin = "0", ClampMax = "3", EditCondition = "bIsBouncing"))
	float GravityFactor = 0.3f;

	/** Collision radius for bouncing projectiles (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bouncing", meta = (ClampMin = "1", ClampMax = "100", EditCondition = "bIsBouncing"))
	float CollisionRadius = 5.0f;
};
