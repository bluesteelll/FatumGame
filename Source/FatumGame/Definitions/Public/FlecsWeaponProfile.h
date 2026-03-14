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
	// WEAPON INERTIA (Weapon Lag)
	// Heavy weapons lag behind crosshair via spring-damper with overshoot.
	// Bullets fire where the weapon points, NOT where the crosshair is.
	// ═══════════════════════════════════════════════════════════════

	/** Spring stiffness. Higher = weapon catches up faster. Pistol~400, LMG~100. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inertia", meta = (ClampMin = "10", ClampMax = "1000"))
	float InertiaStiffness = 200.f;

	/** Damping ratio. <1 = overshoot (momentum carry), 1 = critically damped, >1 = sluggish. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inertia", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float InertiaDamping = 0.7f;

	/** Maximum angular offset from crosshair (degrees). Clamps extreme flicks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inertia", meta = (ClampMin = "0", ClampMax = "15"))
	float MaxInertiaOffset = 3.f;

	/** Idle sway amplitude (degrees). Weapon/crosshair drift when standing still. 0 = disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inertia", meta = (ClampMin = "0", ClampMax = "3"))
	float IdleSwayAmplitude = 0.15f;

	/** Idle sway frequency (Hz). How fast the weapon sways. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inertia", meta = (ClampMin = "0.1", ClampMax = "3"))
	float IdleSwayFrequency = 0.4f;

	/** If true, sway fades out during mouse movement and fades in after 0.5s idle. If false, sway is always active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inertia")
	bool bSwayFadeDuringMouse = false;

	// ═══════════════════════════════════════════════════════════════
	// POSITIONAL INERTIA — weapon mesh shifts on screen ("heavy hands" / Unrecord-style)
	// Mouse yaw right → weapon shifts left. Mouse pitch up → weapon shifts down.
	// ═══════════════════════════════════════════════════════════════

	/** How many cm the weapon shifts per degree of mouse input. Higher = more visible displacement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Positional Inertia", meta = (ClampMin = "0", ClampMax = "2"))
	float InertiaPositionScale = 0.3f;

	/** Positional spring stiffness. Lower = heavier hands, longer return time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Positional Inertia", meta = (ClampMin = "5", ClampMax = "500"))
	float InertiaPositionStiffness = 80.f;

	/** Positional damping ratio. <1 = overshoot, 1 = no oscillation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Positional Inertia", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float InertiaPositionDamping = 0.6f;

	/** Maximum positional offset (cm). Clamps extreme displacements. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Positional Inertia", meta = (ClampMin = "0", ClampMax = "20"))
	float MaxInertiaPositionOffset = 5.f;

	// ═══════════════════════════════════════════════════════════════
	// WEAPON MOTION — Movement-Based Effects (walk bob, strafe tilt, etc.)
	// All effects layer additively on the weapon mesh transform.
	// ═══════════════════════════════════════════════════════════════

	// ── Walk Bob (figure-8 Lissajous pattern driven by movement speed) ──

	/** Horizontal bob amplitude (cm). Side-to-side sway per step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Walk Bob", meta = (ClampMin = "0", ClampMax = "5"))
	float WalkBobAmplitudeH = 0.8f;

	/** Vertical bob amplitude (cm). Up-down bounce per step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Walk Bob", meta = (ClampMin = "0", ClampMax = "5"))
	float WalkBobAmplitudeV = 0.5f;

	/** Bob frequency at full walk speed (Hz). Steps per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Walk Bob", meta = (ClampMin = "1", ClampMax = "20"))
	float WalkBobFrequency = 10.f;

	/** Roll rotation per step cycle (degrees). Adds weapon tilt during bob. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Walk Bob", meta = (ClampMin = "0", ClampMax = "5"))
	float WalkBobRollAmplitude = 0.5f;

	/** Sprint bob multiplier. Amplifies bob during sprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Walk Bob", meta = (ClampMin = "0", ClampMax = "3"))
	float SprintBobMultiplier = 1.5f;

	/** Character speed considered "full walk" for bob scaling (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Walk Bob", meta = (ClampMin = "50", ClampMax = "1000"))
	float WalkBobReferenceSpeed = 300.f;

	// ── Strafe Tilt (weapon rolls when moving laterally) ──

	/** Maximum roll angle when strafing at full speed (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Strafe Tilt", meta = (ClampMin = "0", ClampMax = "10"))
	float StrafeTiltAngle = 2.f;

	/** Interpolation speed toward target tilt. Higher = snappier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Strafe Tilt", meta = (ClampMin = "1", ClampMax = "30"))
	float StrafeTiltSpeed = 8.f;

	// ── Landing Impact (spring impulse on land, scales with fall speed) ──

	/** Displacement per m/s of fall speed (cm). How much weapon drops on landing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Landing Impact", meta = (ClampMin = "0", ClampMax = "10"))
	float LandingImpactScale = 3.f;

	/** Landing spring stiffness. Lower = longer bounce. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Landing Impact", meta = (ClampMin = "10", ClampMax = "500"))
	float LandingSpringStiffness = 150.f;

	/** Landing spring damping. <1 = overshoot bounce, 1 = no oscillation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Landing Impact", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float LandingSpringDamping = 0.6f;

	// ── Sprint Pose (weapon lowers and tilts during sprint) ──

	/** Weapon position offset during sprint (local space, cm). Typically forward+down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Sprint Pose")
	FVector SprintPoseOffset = FVector(5.f, 0.f, -3.f);

	/** Weapon rotation offset during sprint (degrees). Typically tilt down + roll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Sprint Pose")
	FRotator SprintPoseRotation = FRotator(-10.f, 5.f, 10.f);

	/** Transition speed to/from sprint pose. Higher = snappier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Sprint Pose", meta = (ClampMin = "1", ClampMax = "20"))
	float SprintTransitionSpeed = 6.f;

	// ── Movement Inertia (weapon shifts based on character acceleration) ──

	/** Displacement per cm/s of velocity. Higher = weapon shifts more during movement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Movement Inertia", meta = (ClampMin = "0", ClampMax = "0.05"))
	float MovementInertiaScale = 0.005f;

	/** Movement inertia spring stiffness. Lower = heavier weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Movement Inertia", meta = (ClampMin = "5", ClampMax = "300"))
	float MovementInertiaStiffness = 80.f;

	/** Movement inertia damping. <1 = overshoot on stop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Movement Inertia", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float MovementInertiaDamping = 0.7f;

	/** Maximum movement inertia offset (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Motion|Movement Inertia", meta = (ClampMin = "0", ClampMax = "10"))
	float MaxMovementInertiaOffset = 3.f;

	// ═══════════════════════════════════════════════════════════════
	// WEAPON COLLISION (Wall Ready — weapon retracts near obstacles)
	// 3 raycasts from camera detect nearby geometry (NON_MOVING only).
	// Alpha drives smooth blend from hip pose to ready pose.
	// ═══════════════════════════════════════════════════════════════

	/** Maximum raycast distance for wall detection (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Collision", meta = (ClampMin = "10", ClampMax = "200"))
	float CollisionTraceDistance = 80.f;

	/** Weapon starts retracting when obstacle is closer than this (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Collision", meta = (ClampMin = "5", ClampMax = "200"))
	float CollisionStartRetractDistance = 70.f;

	/** Weapon is fully retracted when obstacle is this close (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Collision", meta = (ClampMin = "1", ClampMax = "100"))
	float CollisionFullRetractDistance = 20.f;

	/** Power curve exponent for alpha mapping (1=linear, 2=quadratic ease-in). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Collision", meta = (ClampMin = "0.5", ClampMax = "4"))
	float CollisionAlphaPower = 1.5f;

	/** Weapon position in "ready" pose (local offset from base, cm). Typically raised up+back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Collision")
	FVector CollisionReadyPoseOffset = FVector(-5.f, 0.f, 8.f);

	/** Weapon rotation in "ready" pose when obstacle is below/ahead (degrees). Barrel tips up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Collision")
	FRotator CollisionReadyPoseRotation = FRotator(45.f, 0.f, 0.f);

	/** Weapon position in "low ready" pose (local offset, cm). Used when obstacle is above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Collision")
	FVector CollisionReadyPoseOffsetDown = FVector(-5.f, 0.f, -8.f);

	/** Weapon rotation in "low ready" pose (degrees). Barrel tips down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Collision")
	FRotator CollisionReadyPoseRotationDown = FRotator(-30.f, 0.f, 0.f);

	/** Vertical spread angle for up/down detection rays (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Collision", meta = (ClampMin = "0", ClampMax = "45"))
	float CollisionVerticalSpreadAngle = 15.f;

	/** Speed at which weapon moves INTO ready pose (spring interp speed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Collision", meta = (ClampMin = "1", ClampMax = "30"))
	float CollisionRetractSpeed = 12.f;

	/** Speed at which weapon returns FROM ready pose (spring interp speed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Collision", meta = (ClampMin = "1", ClampMax = "30"))
	float CollisionRestoreSpeed = 6.f;

	/** Alpha threshold above which firing is blocked (0-1). 0 = never block. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Collision", meta = (ClampMin = "0", ClampMax = "1"))
	float CollisionFireBlockThreshold = 0.7f;

	/** Ray spread half-angle (degrees). Side rays fan out by this amount. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Collision", meta = (ClampMin = "0", ClampMax = "30"))
	float CollisionRaySpreadAngle = 10.f;

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
