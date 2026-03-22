// Ammo type definition — defines what projectile spawns when this round fires.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsAmmoTypeDefinition.generated.h"

class UFlecsEntityDefinition;

UCLASS(BlueprintType)
class FATUMGAME_API UFlecsAmmoTypeDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Internal name for this ammo type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FName AmmoName;

	/** Display name for UI */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FText DisplayName;

	/** Caliber index from CaliberRegistry (Project Settings → Fatum Game). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Caliber", meta = (GetOptions = "GetCaliberOptions"))
	FName Caliber;

	/** Projectile entity definition to spawn when this round fires.
	 *  Must have ProjectileProfile + PhysicsProfile + RenderProfile.
	 *  DamageProfile on this definition provides base damage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile")
	TObjectPtr<UFlecsEntityDefinition> ProjectileDefinition;

	/** Damage multiplier on top of projectile's base damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multipliers", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float DamageMultiplier = 1.0f;

	/** Speed multiplier on top of projectile's base speed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multipliers", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float SpeedMultiplier = 1.0f;

	/** Icon for UI display */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	TObjectPtr<UTexture2D> Icon;

	UFUNCTION()
	TArray<FName> GetCaliberOptions() const;
};
