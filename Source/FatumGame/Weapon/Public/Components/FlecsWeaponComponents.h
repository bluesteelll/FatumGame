// Weapon components for Flecs ECS.
// Static data in prefab, instance data per-entity.

#pragma once

#include "CoreMinimal.h"
#include "FlecsWeaponComponents.generated.h"

class UFlecsEntityDefinition;
class UFlecsWeaponProfile;
class USkeletalMesh;
class UStaticMesh;
class UAnimMontage;

// ═══════════════════════════════════════════════════════════════
// AIM DIRECTION (Per-Character)
// ═══════════════════════════════════════════════════════════════
//
// Aim direction for shooting. Updated by player input or AI.
// WeaponFireSystem reads this to determine projectile direction.
//
// Usage:
//   // Player: update from camera
//   FAimDirection* Aim = CharacterEntity.get_mut<FAimDirection>();
//   Aim->Direction = CameraForward;
//
//   // AI: update towards target
//   Aim->Direction = (TargetLocation - MyLocation).GetSafeNormal();
// ═══════════════════════════════════════════════════════════════

struct FAimDirection
{
	/** Normalized aim direction in world space */
	FVector Direction = FVector::ForwardVector;

	/** Projectile spawn origin in world space (camera position for players, actor pos for AI).
	 *  Using camera position eliminates crosshair parallax — projectiles fly exactly where the crosshair points.
	 *  WeaponFireSystem uses this for aim raycast origin. */
	FVector CharacterPosition = FVector::ZeroVector;

	/** Actual weapon muzzle world position, updated from weapon mesh socket each tick.
	 *  Follows weapon animations (recoil, sway, etc).
	 *  WeaponFireSystem uses this as projectile spawn location.
	 *  Fallback: if zero, sim thread computes from CharacterPosition + FWeaponStatic::MuzzleOffset. */
	FVector MuzzleWorldPosition = FVector::ZeroVector;
};

// ═══════════════════════════════════════════════════════════════
// WEAPON STATIC (Prefab)
// ═══════════════════════════════════════════════════════════════
//
// Static weapon data - lives in PREFAB, shared by all weapons of this type.
// Contains immutable weapon configuration derived from UFlecsWeaponProfile.
//
// Usage:
//   const FWeaponStatic* Static = WeaponEntity.try_get<FWeaponStatic>(); // from prefab
//   FWeaponInstance* Instance = WeaponEntity.get_mut<FWeaponInstance>();  // from entity
// ═══════════════════════════════════════════════════════════════

struct FWeaponStatic
{
	// ─────────────────────────────────────────────────────────
	// FIRING
	// ─────────────────────────────────────────────────────────

	/** Projectile definition to spawn (from WeaponProfile) */
	UFlecsEntityDefinition* ProjectileDefinition = nullptr;

	/** Time between shots in seconds (e.g., 0.1 = 10 shots/sec) */
	float FireInterval = 0.1f;

	/** Burst count for burst fire mode */
	int32 BurstCount = 1;

	/** Delay between bursts in seconds */
	float BurstDelay = 0.3f;

	/** Projectile speed multiplier */
	float ProjectileSpeedMultiplier = 1.0f;

	/** Damage multiplier */
	float DamageMultiplier = 1.0f;

	/** Projectiles per shot (>1 for shotguns) */
	int32 ProjectilesPerShot = 1;

	/** Is automatic fire? */
	bool bIsAutomatic = false;

	/** Is burst fire? */
	bool bIsBurst = false;

	// ─────────────────────────────────────────────────────────
	// AMMO & RELOAD
	// ─────────────────────────────────────────────────────────

	/** Accepted caliber IDs (from CaliberRegistry). 0xFF = invalid. */
	static constexpr int32 MaxAcceptedCalibers = 4;
	uint8 AcceptedCaliberIds[MaxAcceptedCalibers] = {0xFF, 0xFF, 0xFF, 0xFF};
	int32 AcceptedCaliberCount = 0;

	/** Check if a caliber ID is accepted by this weapon. */
	bool AcceptsCaliber(uint8 CaliberId) const
	{
		for (int32 i = 0; i < AcceptedCaliberCount; ++i)
			if (AcceptedCaliberIds[i] == CaliberId) return true;
		return false;
	}

	/** Ammo consumed per shot */
	int32 AmmoPerShot = 1;

	/** Weapon has a chamber (+1 round). Tactical reload skips chambering. */
	bool bHasChamber = true;

	/** Unlimited ammo (debug). Ignores magazine system, uses ProjectileDefinition. */
	bool bUnlimitedAmmo = false;

	/** Reload phase durations (seconds) */
	float RemoveMagTime = 0.5f;
	float InsertMagTime = 0.7f;
	float ChamberTime = 0.3f;

	/** Movement speed multiplier during reload */
	float ReloadMoveSpeedMultiplier = 0.7f;

	// ─────────────────────────────────────────────────────────
	// MUZZLE
	// ─────────────────────────────────────────────────────────

	/** Muzzle offset from weapon origin */
	FVector MuzzleOffset = FVector(50.f, 0.f, 0.f);

	/** Muzzle socket name (optional) */
	FName MuzzleSocketName = NAME_None;

	// ─────────────────────────────────────────────────────────
	// VISUALS
	// Pointers are safe because DataAssets persist for game lifetime.
	// ─────────────────────────────────────────────────────────

	/** Skeletal mesh when equipped (for animations) */
	USkeletalMesh* EquippedMesh = nullptr;

	/** Static mesh when dropped (for ISM) */
	UStaticMesh* DroppedMesh = nullptr;

	/** Attach socket on character */
	FName AttachSocket = TEXT("weapon_r");

	/** Local transform offset when attached */
	FTransform AttachOffset;

	/** Scale when dropped */
	FVector DroppedScale = FVector::OneVector;

	// ─────────────────────────────────────────────────────────
	// ANIMATIONS
	// ─────────────────────────────────────────────────────────

	/** Fire animation montage */
	UAnimMontage* FireMontage = nullptr;

	/** Reload animation montage */
	UAnimMontage* ReloadMontage = nullptr;

	/** Equip animation montage */
	UAnimMontage* EquipMontage = nullptr;

	// ─────────────────────────────────────────────────────────
	// SPREAD & BLOOM (all values in decidegrees, 1 unit = 0.1°)
	// ─────────────────────────────────────────────────────────

	float BaseSpread = 3.f;       // standing inaccuracy (always present)
	float SpreadPerShot = 5.f;    // bloom growth per shot
	float MaxBloom = 50.f;        // max bloom cap
	float BloomDecayRate = 100.f; // bloom decay speed (decideg/sec)
	float BloomRecoveryDelay = 0.1f;

	/** Per-movement-state multipliers. Indexed by EWeaponMoveState. */
	static constexpr int32 NumMoveStates = 6;
	float BaseSpreadMultipliers[NumMoveStates] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
	float BloomMultipliers[NumMoveStates] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f};

	static FWeaponStatic FromProfile(const UFlecsWeaponProfile* Profile, const class UFlecsCaliberRegistry* CaliberRegistry = nullptr);
};

/** Weapon reload phase state machine. */
enum class EWeaponReloadPhase : uint8
{
	Idle = 0,
	RemovingMag = 1,
	InsertingMag = 2,
	Chambering = 3
};

// ═══════════════════════════════════════════════════════════════
// WEAPON INSTANCE (Per-Entity)
// ═══════════════════════════════════════════════════════════════
//
// Instance weapon data - mutable per-entity data.
// Static data (fire rate, caliber) comes from FWeaponStatic in prefab.
// ═══════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FWeaponInstance
{
	GENERATED_BODY()

	// ─────────────────────────────────────────────────────────
	// MAGAZINE STATE
	// ─────────────────────────────────────────────────────────

	/** Flecs entity ID of the currently inserted magazine. 0 = no magazine. */
	int64 InsertedMagazineId = 0;

	// ─────────────────────────────────────────────────────────
	// FIRING STATE (countdown timers - subtract dt, fire when <= 0)
	// ─────────────────────────────────────────────────────────

	/** Countdown: seconds until weapon can fire again. <= 0 means ready.
	 *  Initialized to 0 so weapon can fire immediately after creation.
	 *  On fire: set to FireInterval. Each tick: -= DeltaTime. */
	float FireCooldownRemaining = 0.f;

	/** Remaining shots in current burst */
	int32 BurstShotsRemaining = 0;

	/** Burst cooldown time remaining in seconds */
	float BurstCooldownRemaining = 0.f;

	/** For semi-auto: already fired while trigger held, must release first */
	bool bHasFiredSincePress = false;

	// ─────────────────────────────────────────────────────────
	// RELOAD STATE
	// ─────────────────────────────────────────────────────────

	/** Current reload phase */
	EWeaponReloadPhase ReloadPhase = EWeaponReloadPhase::Idle;

	/** Timer for current reload phase (counts down to 0) */
	float ReloadPhaseTimer = 0.f;

	/** Magazine entity selected for insertion during reload */
	int64 SelectedMagazineId = 0;

	/** Was the previous magazine empty? (determines if chambering is needed) */
	bool bPrevMagWasEmpty = false;

	// ─────────────────────────────────────────────────────────
	// BLOOM STATE
	// ─────────────────────────────────────────────────────────

	/** Current bloom in decidegrees (grows on fire, decays to 0). BaseSpread added separately. */
	float CurrentBloom = 0.f;

	/** Seconds since last successful fire (for recovery delay) */
	float TimeSinceLastShot = 999.f;

	/** Total shots fired since equip (monotonic, for pattern recoil indexing on game thread) */
	int32 ShotsFiredTotal = 0;

	// ─────────────────────────────────────────────────────────
	// INPUT FLAGS (Game Thread → Simulation Thread)
	// Set by game thread via EnqueueCommand, consumed by systems.
	// ─────────────────────────────────────────────────────────

	/** Fire button is held (continuous: true while pressed, false on release) */
	bool bFireRequested = false;

	/** Latched fire trigger: set on press, consumed only after successful fire.
	 *  Survives Start+Stop command batching in the command queue. */
	bool bFireTriggerPending = false;

	/** Reload was requested */
	bool bReloadRequested = false;

	/** Reload cancel was requested (press R during reload) */
	bool bReloadCancelRequested = false;

	// ─────────────────────────────────────────────────────────
	// HELPERS
	// ─────────────────────────────────────────────────────────

	/** Check if weapon can fire (cooldown expired, has magazine, not reloading) */
	bool CanFire() const
	{
		return ReloadPhase == EWeaponReloadPhase::Idle
			&& InsertedMagazineId != 0
			&& FireCooldownRemaining <= 0.f
			&& BurstCooldownRemaining <= 0.f;
	}

	/** Check if weapon is currently reloading */
	bool IsReloading() const { return ReloadPhase != EWeaponReloadPhase::Idle; }
};

// ═══════════════════════════════════════════════════════════════
// EQUIPPED BY (Relationship)
// ═══════════════════════════════════════════════════════════════
//
// Marks weapon as equipped by a character.
// Similar to FContainedIn but for weapons.
//
// When present: weapon uses SkeletalMesh attached to character socket.
// When absent:  weapon exists in world with StaticMesh + Barrage physics.
//
// Usage:
//   // Equip
//   FEquippedBy Equipped;
//   Equipped.CharacterEntityId = CharacterEntity.id();
//   Equipped.SlotId = 0;
//   WeaponEntity.set<FEquippedBy>(Equipped);
//
//   // Query equipped weapons
//   world.each([](flecs::entity E, const FEquippedBy& Eq) {
//       if (Eq.CharacterEntityId == MyCharId) { ... }
//   });
//
//   // Unequip
//   WeaponEntity.remove<FEquippedBy>();
// ═══════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FEquippedBy
{
	GENERATED_BODY()

	/** Flecs entity ID of the character wielding this weapon */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon")
	int64 CharacterEntityId = 0;

	/** Equipment slot identifier (0 = primary, 1 = secondary, etc.) */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon")
	int32 SlotId = 0;

	/** Check if weapon is equipped */
	bool IsEquipped() const { return CharacterEntityId != 0; }
};

// ═══════════════════════════════════════════════════════════════
// TAG
// ═══════════════════════════════════════════════════════════════

/** Entity is a weapon */
struct FTagWeapon {};

/** Weapon is currently reloading (query optimization) */
struct FTagReloading {};
