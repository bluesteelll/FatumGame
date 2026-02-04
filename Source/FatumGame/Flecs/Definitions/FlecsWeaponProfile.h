// Weapon profile for Flecs entity spawning.
// Defines firing behavior, ammo, and visual configuration.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsWeaponProfile.generated.h"

class UFlecsEntityDefinition;
class UStaticMesh;
class USkeletalMesh;
class UAnimMontage;

/**
 * Weapon firing mode.
 */
UENUM(BlueprintType)
enum class EWeaponFireMode : uint8
{
	SemiAuto	UMETA(DisplayName = "Semi-Automatic"),
	FullAuto	UMETA(DisplayName = "Full-Automatic"),
	Burst		UMETA(DisplayName = "Burst Fire")
};

/**
 * Weapon profile - defines weapon behavior and configuration.
 *
 * Usage:
 * - Create UFlecsEntityDefinition with WeaponProfile
 * - WeaponProfile.ProjectileDefinition = what to spawn when firing
 * - Spawn weapon entity, equip to character
 * - WeaponFireSystem handles firing logic on Artillery thread
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsWeaponProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// FIRING
	// ═══════════════════════════════════════════════════════════════

	/** Projectile definition to spawn when firing (must have ProjectileProfile) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Firing")
	TObjectPtr<UFlecsEntityDefinition> ProjectileDefinition;

	/** Fire mode */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Firing")
	EWeaponFireMode FireMode = EWeaponFireMode::SemiAuto;

	/** Rounds per minute (600 = 10 shots/sec) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Firing", meta = (ClampMin = "1", ClampMax = "2000"))
	float FireRate = 600.f;

	/** Burst count (for burst fire mode) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Firing", meta = (ClampMin = "2", ClampMax = "10", EditCondition = "FireMode == EWeaponFireMode::Burst", EditConditionHides))
	int32 BurstCount = 3;

	/** Projectile speed multiplier (1.0 = use projectile default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Firing", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float ProjectileSpeedMultiplier = 1.0f;

	/** Damage multiplier applied to projectile damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Firing", meta = (ClampMin = "0.1"))
	float DamageMultiplier = 1.0f;

	/** Projectiles per shot (>1 for shotguns) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Firing", meta = (ClampMin = "1", ClampMax = "20"))
	int32 ProjectilesPerShot = 1;

	// ═══════════════════════════════════════════════════════════════
	// AMMUNITION
	// ═══════════════════════════════════════════════════════════════

	/** Magazine capacity (-1 = unlimited) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammunition", meta = (ClampMin = "-1"))
	int32 MagazineSize = 30;

	/** Reload time in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammunition", meta = (ClampMin = "0"))
	float ReloadTime = 2.0f;

	/** Max reserve ammo (-1 = unlimited) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammunition", meta = (ClampMin = "-1"))
	int32 MaxReserveAmmo = 300;

	/** Ammo consumed per shot */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammunition", meta = (ClampMin = "1"))
	int32 AmmoPerShot = 1;

	// ═══════════════════════════════════════════════════════════════
	// MUZZLE
	// ═══════════════════════════════════════════════════════════════

	/** Muzzle offset from weapon origin (when equipped) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muzzle")
	FVector MuzzleOffset = FVector(50.f, 0.f, 0.f);

	/** Socket name on skeletal mesh for muzzle (alternative to offset) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Muzzle")
	FName MuzzleSocketName = NAME_None;

	// ═══════════════════════════════════════════════════════════════
	// VISUALS - EQUIPPED
	// ═══════════════════════════════════════════════════════════════

	/** Skeletal mesh when equipped (for animations) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Equipped")
	TObjectPtr<USkeletalMesh> EquippedMesh;

	/** Attach socket on character */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Equipped")
	FName AttachSocket = TEXT("weapon_r");

	/** Local offset when attached */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Equipped")
	FTransform AttachOffset;

	// ═══════════════════════════════════════════════════════════════
	// VISUALS - DROPPED
	// ═══════════════════════════════════════════════════════════════

	/** Static mesh when dropped (for ISM rendering) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Dropped")
	TObjectPtr<UStaticMesh> DroppedMesh;

	/** Scale when dropped */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Dropped")
	FVector DroppedScale = FVector::OneVector;

	// ═══════════════════════════════════════════════════════════════
	// ANIMATIONS
	// ═══════════════════════════════════════════════════════════════

	/** Fire animation montage (played on character) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
	TObjectPtr<UAnimMontage> FireMontage;

	/** Reload animation montage (played on character) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
	TObjectPtr<UAnimMontage> ReloadMontage;

	/** Equip animation montage (played on character) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
	TObjectPtr<UAnimMontage> EquipMontage;

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	/** Get fire interval in seconds */
	float GetFireInterval() const { return 60.f / FMath::Max(1.f, FireRate); }

	/** Get fire interval in frames (120Hz Artillery tick) */
	int32 GetFireIntervalFrames() const { return FMath::RoundToInt(GetFireInterval() * 120.f); }

	/** Get reload time in frames (120Hz) */
	int32 GetReloadTimeFrames() const { return FMath::RoundToInt(ReloadTime * 120.f); }

	/** Check if weapon has unlimited ammo */
	bool HasUnlimitedAmmo() const { return MagazineSize < 0; }

	/** Check if weapon has unlimited reserve */
	bool HasUnlimitedReserve() const { return MaxReserveAmmo < 0; }

	/** Check if weapon is a shotgun (multiple projectiles) */
	bool IsShotgun() const { return ProjectilesPerShot > 1; }

	/** Check if weapon is automatic */
	bool IsAutomatic() const { return FireMode == EWeaponFireMode::FullAuto; }

	/** Check if weapon is burst */
	bool IsBurst() const { return FireMode == EWeaponFireMode::Burst; }
};
