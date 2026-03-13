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
class UCurveVector;

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
 * - WeaponFireSystem handles firing logic on simulation thread
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

	/** Local offset when attached (relative to camera in FPS mode) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Equipped")
	FTransform AttachOffset = FTransform(FRotator::ZeroRotator, FVector(30.f, 15.f, -15.f));

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
	// BLOOM (Spread)
	// Random projectile deviation within an expanding cone.
	// First shot accuracy: BaseSpread=0 means perfectly accurate first shot.
	// ═══════════════════════════════════════════════════════════════

	/** Minimum spread cone half-angle in degrees. 0 = first-shot-accurate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom", meta = (ClampMin = "0", ClampMax = "30"))
	float BaseSpread = 0.f;

	/** Spread growth per shot (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom", meta = (ClampMin = "0", ClampMax = "10"))
	float SpreadPerShot = 0.5f;

	/** Maximum spread cone half-angle (degrees). Cap during sustained fire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom", meta = (ClampMin = "0", ClampMax = "30"))
	float MaxSpread = 5.f;

	/** Spread decay speed (degrees/sec) when not firing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom", meta = (ClampMin = "0", ClampMax = "100"))
	float SpreadDecayRate = 10.f;

	/** Seconds after last shot before spread starts decaying. Prevents tap-tap abuse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom", meta = (ClampMin = "0", ClampMax = "2"))
	float SpreadRecoveryDelay = 0.1f;

	/** Additional spread (degrees) when character is moving. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom", meta = (ClampMin = "0", ClampMax = "10"))
	float MovingSpreadAdd = 1.f;

	/** Additional spread (degrees) when character is airborne. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bloom", meta = (ClampMin = "0", ClampMax = "15"))
	float JumpingSpreadAdd = 2.f;

	// ═══════════════════════════════════════════════════════════════
	// SCREEN SHAKE (Visual Only)
	// Camera oscillation that does NOT affect aim direction.
	// Applied as AddLocalRotation on camera, not on ControlRotation.
	// ═══════════════════════════════════════════════════════════════

	/** Shake amplitude per shot (degrees). Additive — stacks with rapid fire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen Shake", meta = (ClampMin = "0", ClampMax = "5"))
	float ShakeAmplitude = 0.3f;

	/** Shake oscillation frequency (Hz). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen Shake", meta = (ClampMin = "1", ClampMax = "60"))
	float ShakeFrequency = 25.f;

	/** Shake exponential decay speed. Higher = faster fadeout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen Shake", meta = (ClampMin = "1", ClampMax = "100"))
	float ShakeDecaySpeed = 15.f;

	/** Roll (camera tilt) amplitude per shot (degrees). Adds mechanical rattle feel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screen Shake", meta = (ClampMin = "0", ClampMax = "5"))
	float ShakeRollAmplitude = 0.4f;

	// ═══════════════════════════════════════════════════════════════
	// KICK (View Punch) — Auto-Recovering
	// Short random camera displacement that springs back to zero.
	// Temporarily affects aim but player doesn't need to compensate.
	// ═══════════════════════════════════════════════════════════════

	/** Min pitch kick per shot (degrees). Negative = upward kick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick", meta = (ClampMin = "-5", ClampMax = "5"))
	float KickPitchMin = -0.8f;

	/** Max pitch kick per shot (degrees). Negative = upward kick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick", meta = (ClampMin = "-5", ClampMax = "5"))
	float KickPitchMax = -0.3f;

	/** Min yaw kick per shot (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick", meta = (ClampMin = "-5", ClampMax = "5"))
	float KickYawMin = -0.2f;

	/** Max yaw kick per shot (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick", meta = (ClampMin = "-5", ClampMax = "5"))
	float KickYawMax = 0.2f;

	/** Spring angular frequency for kick recovery (rad/s). Higher = faster snap-back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick", meta = (ClampMin = "1", ClampMax = "50"))
	float KickRecoverySpeed = 10.f;

	/** Damping ratio for kick spring. 0.7 = slightly underdamped (natural feel), 1.0 = critically damped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float KickDamping = 0.7f;

	// ═══════════════════════════════════════════════════════════════
	// PATTERN RECOIL — Player Must Compensate
	// Deterministic camera displacement from a per-shot table.
	// Player pulls mouse to counter. Does NOT auto-recover.
	// ═══════════════════════════════════════════════════════════════

	/** Recoil pattern curve. X axis = shot index, channels: X=Pitch, Y=Yaw (degrees).
	 *  Create via Content Browser → Miscellaneous → Curve Vector.
	 *  nullptr = no pattern recoil. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern Recoil")
	TObjectPtr<UCurveVector> RecoilPatternCurve;

	/** Global multiplier on all pattern deltas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern Recoil", meta = (ClampMin = "0", ClampMax = "5"))
	float PatternScale = 1.f;

	/** Random pitch perturbation per shot (degrees). Added on top of pattern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern Recoil", meta = (ClampMin = "0", ClampMax = "2"))
	float PatternRandomPitch = 0.1f;

	/** Random yaw perturbation per shot (degrees). Added on top of pattern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern Recoil", meta = (ClampMin = "0", ClampMax = "2"))
	float PatternRandomYaw = 0.1f;

	/** Seconds after last shot before ShotIndex resets to 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern Recoil", meta = (ClampMin = "0.05", ClampMax = "3"))
	float PatternResetTime = 0.3f;

	/** When ShotIndex exceeds array, loop from this index. -1 = clamp to last entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern Recoil", meta = (ClampMin = "-1"))
	int32 PatternLoopStartIndex = -1;

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	/** Get fire interval in seconds */
	float GetFireInterval() const { return 60.f / FMath::Max(1.f, FireRate); }

	/** Get fire interval in frames (60Hz simulation tick) */
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
