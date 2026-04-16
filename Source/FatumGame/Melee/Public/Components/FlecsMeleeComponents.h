// Melee weapon components for Flecs ECS.
// Static data in prefab, instance data per-entity.
// Mirrors the ranged FWeaponStatic / FWeaponInstance pattern but covers a
// fundamentally different state machine (swing phases, blade socket sweep,
// block absorb, charge).
//
// Blueprint reference: Melee Combat System Rev 3, §A.1.

#pragma once

#include "CoreMinimal.h"
#include "Containers/TripleBuffer.h"
#include "FlecsPenetrationComponents.h" // FPenetrationStatic (reused inline)
#include "FlecsMeleeComponents.generated.h"

class UFlecsMeleeProfile;
class UCurveFloat;
class UNiagaraSystem;

// ═══════════════════════════════════════════════════════════════
// ENUMS
// ═══════════════════════════════════════════════════════════════

UENUM(BlueprintType)
enum class EMeleeSwingDirection : uint8
{
	Horizontal  = 0,
	Vertical    = 1,
	DiagonalTL  = 2,
	DiagonalTR  = 3,
	Thrust      = 4
};

UENUM(BlueprintType)
enum class EMeleeAttackPhase : uint8
{
	Idle     = 0,
	Charging = 1,
	Windup   = 2,
	Release  = 3,
	Recovery = 4
};

UENUM(BlueprintType)
enum class EMeleeDamageDelivery : uint8
{
	Slashing = 0,
	Blunt    = 1,
	Piercing = 2
};

UENUM(BlueprintType)
enum class EBlockTimingQuality : uint8
{
	Normal  = 0,
	Perfect = 1
};

// ═══════════════════════════════════════════════════════════════
// PLAIN STRUCTS (no GENERATED_BODY — inline members of instance)
// ═══════════════════════════════════════════════════════════════

/** Charged-swing payload emitted by MeleeChargeSystem, consumed by MeleeSwingInitSystem.
 *  Plain struct (NO GENERATED_BODY) — lives inline inside FMeleeWeaponInstance.
 *  bValid=false sentinel is the "empty" state. Mirror of FChargeShotPayload. */
struct FMeleeChargePayload
{
	bool  bValid           = false;
	float ShapedT          = 0.f;
	float DamageMul        = 1.f;
	float ImpulseMul       = 1.f;
	float PenetrationMul   = 1.f;
	float SwingSpeedMul    = 1.f;
	float StaminaCostScale = 1.f;
	EMeleeSwingDirection Direction = EMeleeSwingDirection::Horizontal;
};

/** Per-swing penetration state (blueprint C3 — replaces thread_local scratch).
 *  Reset by MeleeSwingInitSystem at the start of every new swing. */
struct FSwingPenState
{
	float  RemainingBudget         = 0.f;
	float  CurrentDamageMultiplier = 1.f;
	int32  PenetrationCount        = 0;
	uint64 LastPenetratedTargetId  = 0;
};

// ═══════════════════════════════════════════════════════════════
// MELEE WEAPON STATIC (Prefab)
// USTRUCT + GENERATED_BODY + UPROPERTY on UObject refs — mirrors ranged
// FWeaponStatic UObject-UPROPERTY pattern (blueprint C6).
// ═══════════════════════════════════════════════════════════════

USTRUCT()
struct FMeleeWeaponStatic
{
	GENERATED_BODY()

	// ─────────────────────────────────────────────────────────
	// GEOMETRY
	// ─────────────────────────────────────────────────────────

	/** Hilt-end socket on the skeletal mesh (capsule start). */
	FName BladeStartSocket = TEXT("hilt_base");

	/** Tip socket on the skeletal mesh (capsule end). */
	FName BladeTipSocket = TEXT("blade_tip");

	/** Effective blade length (cm). Used for range checks and future tuning. */
	float Reach = 120.f;

	/** Capsule radius (cm) used by the sweep. */
	float CapsuleRadius = 4.f;

	/** Retained for future ragdoll tuning; NOT used in impulse formula (M5). */
	float WeaponMass = 1.5f;

	/** Number of slerped capsule substeps per sim tick. [1, 8]. */
	int32 SweepSubstepsPerTick = 3;

	// ─────────────────────────────────────────────────────────
	// PHASE TIMINGS (seconds)
	// ─────────────────────────────────────────────────────────

	float WindupTime   = 0.20f;
	float ReleaseTime  = 0.18f;
	float RecoveryTime = 0.40f;

	// ─────────────────────────────────────────────────────────
	// DAMAGE
	// ─────────────────────────────────────────────────────────

	float BaseDamage     = 40.f;
	float BaseImpulse    = 500.f;
	float CritChance     = 0.05f;
	float CritMultiplier = 2.f;

	EMeleeDamageDelivery DeliveryType = EMeleeDamageDelivery::Slashing;

	/** Reference tip speed (cm/s) at which TipSpeedMul == 1.0. */
	float ReferenceTipSpeed = 1200.f;

	/** Tip-speed multiplier clamp [min, max]. */
	float TipSpeedMulMin = 0.4f;
	float TipSpeedMulMax = 1.4f;

	// ─────────────────────────────────────────────────────────
	// PENETRATION (inline — MVP choice per blueprint §J Q8)
	// ─────────────────────────────────────────────────────────

	FPenetrationStatic MeleePen;

	// ─────────────────────────────────────────────────────────
	// CHARGE
	// ─────────────────────────────────────────────────────────

	bool  bEnableCharge        = true;
	float MinChargeTime        = 0.15f;
	float MaxChargeTime        = 0.90f;
	bool  bAutoFireAtMaxCharge = false;
	bool  bAutoRestartCharge   = false;

	/** Optional shaping curve mapping raw charge [0,1] → shaped [0,1]. Null = linear.
	 *  Game-thread reads only (matches ranged FWeaponStatic pattern). */
	UPROPERTY()
	TObjectPtr<UCurveFloat> ChargeCurve = nullptr;

	float DamageMaxMultiplier      = 1.8f;
	float ImpulseMaxMultiplier     = 1.8f;
	float PenetrationMaxMultiplier = 1.5f;
	float SwingSpeedMaxMultiplier  = 1.15f;
	float StaminaCostMaxMultiplier = 2.0f;

	// ─────────────────────────────────────────────────────────
	// STAMINA
	// ─────────────────────────────────────────────────────────

	float StaminaCostPerSwing       = 15.f;
	float BaseBlockStaminaCost      = 20.f;
	float StaminaCostPerBlockSecond = 5.f;

	// ─────────────────────────────────────────────────────────
	// BLOCK — continuous quality (Q8)
	// ─────────────────────────────────────────────────────────

	bool  bCanBlock                      = true;
	float MinBlockAbsorb                 = 0.10f;
	float MaxBlockAbsorb                 = 0.95f;
	float DirectionalDiscountFactor      = 0.50f;
	float PerfectBlockWindowSeconds      = 0.15f;
	float PerfectTimingStaminaMultiplier = 0.5f;

	// ─────────────────────────────────────────────────────────
	// REBOUND
	// ─────────────────────────────────────────────────────────

	/** Recovery-duration multiplier applied when Slashing hits Metal/Armor_Plate
	 *  (one-shot per swing, §B.2 MeleeSweepSystem step 6). 1.0 = no extension. */
	float ReboundRecoveryMultiplier = 1.0f;

	// ─────────────────────────────────────────────────────────
	// VFX
	// ─────────────────────────────────────────────────────────

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> TrailEffect = nullptr;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> ImpactEffectOverride = nullptr;

	static FMeleeWeaponStatic FromProfile(const UFlecsMeleeProfile* Profile);
};

// ═══════════════════════════════════════════════════════════════
// MELEE WEAPON INSTANCE (Per-Entity)
// ═══════════════════════════════════════════════════════════════

/** Forward-declared triple buffer data type — defined as a nested struct inside
 *  FMeleeWeaponInstance below. Pointer is opaque until the full struct is visible. */
struct FMeleeWeaponInstance;

USTRUCT()
struct FMeleeWeaponInstance
{
	GENERATED_BODY()

	/** Nested POD for the blade-socket triple buffer payload (M4, N-C3).
	 *  Writer: game thread (AFlecsCharacter::Tick). Reader: sim (MeleeSweepSystem). */
	struct FBladeSocketData
	{
		FVector Start      = FVector::ZeroVector;
		FVector Tip        = FVector::ZeroVector;
		uint64  FrameStamp = 0;
	};

	// ─────────────────────────────────────────────────────────
	// CHARGE PAYLOADS (strict ownership — mirror ranged contract)
	// ─────────────────────────────────────────────────────────

	/** Written by MeleeChargeSystem on release. Cleared after promotion. */
	FMeleeChargePayload PendingPayload;

	/** Written by MeleeSwingInitSystem (Pending→Latched promote). Cleared on Idle entry. */
	FMeleeChargePayload LatchedPayload;

	// ─────────────────────────────────────────────────────────
	// INPUT FLAGS (Game Thread → Simulation Thread)
	// ─────────────────────────────────────────────────────────

	bool bAttackRequested            = false;
	bool bBlockRequested             = false;
	/** Previous-tick snapshot of bAttackRequested — edge-trigger release detection (N-m3). */
	bool bWasAttackRequestedLastTick = false;

	// ─────────────────────────────────────────────────────────
	// CHARGE STATE (sim-thread only)
	// ─────────────────────────────────────────────────────────

	bool  bIsCharging         = false;
	float ChargeAccumulator   = 0.f;
	bool  bPendingAutoRestart = false;

	// ─────────────────────────────────────────────────────────
	// PHASE STATE
	// ─────────────────────────────────────────────────────────

	EMeleeAttackPhase    Phase             = EMeleeAttackPhase::Idle;
	float                PhaseTimer        = 0.f;
	float                WindupDuration    = 0.f;
	float                ReleaseDuration   = 0.f;
	float                RecoveryDuration  = 0.f;
	EMeleeSwingDirection ResolvedDirection = EMeleeSwingDirection::Horizontal;
	float                ShapedT           = 0.f;

	// ─────────────────────────────────────────────────────────
	// PER-SWING PENETRATION STATE (C3 — reset at SwingInit)
	// ─────────────────────────────────────────────────────────

	FSwingPenState PenState;

	// ─────────────────────────────────────────────────────────
	// HIT DEDUP (M1 — raw fixed array, linear search)
	// UPROPERTY not supported on plain C-arrays of this shape; kept as raw member.
	// ─────────────────────────────────────────────────────────

	static constexpr int32 MaxHitsPerSwing = 32;
	uint64 HitTargetIds[MaxHitsPerSwing] = {};
	int32  HitCount = 0;

	// ─────────────────────────────────────────────────────────
	// SWEEP STATE
	// ─────────────────────────────────────────────────────────

	FVector PrevBladeStartWS    = FVector::ZeroVector;
	FVector PrevBladeTipWS      = FVector::ZeroVector;
	uint64  LastSweepFrameStamp = 0;
	float   LastTipSpeed        = 0.f;
	/** One-shot flag: true after the first metal/armor Slashing rebound this swing. */
	bool    bSwingRebounded     = false;

	// ─────────────────────────────────────────────────────────
	// BLOCK STATE
	// ─────────────────────────────────────────────────────────

	bool  bIsBlocking            = false;
	/** Set from FSimulationWorker wall-clock RealDT accumulator — NOT Flecs world.time()
	 *  (N-M2: perfect-block timing must not dilate with hit-stop). */
	float BlockStartTimestampSim = 0.f;

	// ─────────────────────────────────────────────────────────
	// PER-WEAPON BLADE SOCKET TRIPLE BUFFER (M4)
	// Raw pointer — UHT does not permit TUniquePtr inside USTRUCT (C4150 /
	// MEMORY.md "TUniquePtr in UHT Headers"). Lifetime: allocated on melee-equip
	// complete, deleted in the Flecs on_remove hook registered in
	// UFlecsArtillerySubsystem::RegisterFlecsComponents (N-C2).
	//
	// Simple-AI path (N-C3): BladeBuffer == nullptr → MeleeSweepSystem reads
	// FBladeSocketSync component on the weapon entity instead.
	// ─────────────────────────────────────────────────────────

	TTripleBuffer<FBladeSocketData>* BladeBuffer = nullptr;

	// ─────────────────────────────────────────────────────────
	// HELPERS
	// ─────────────────────────────────────────────────────────

	/** Zeroes every charge/swing/phase field. Does NOT touch BladeBuffer — lifetime
	 *  is owned exclusively by WeaponEquipSystem (equip alloc) and the Flecs on_remove
	 *  hook (any-path delete). Safe to call from sim thread during unequip prep. */
	void ResetAllChargeAndSwingState();
};

/** Convenience typedef (N-m1). */
using FBladeSocketTripleBuffer = TTripleBuffer<FMeleeWeaponInstance::FBladeSocketData>;

// ═══════════════════════════════════════════════════════════════
// PER-CHARACTER COMPONENTS
// ═══════════════════════════════════════════════════════════════

/** Ring buffer of recent mouse-look deltas, fed by MeleeInputResolveSystem
 *  and consumed by MeleeSwingInitSystem to resolve swing direction. */
USTRUCT()
struct FMeleeAttackDirectionBuffer
{
	GENERATED_BODY()

	static constexpr int32 Capacity = 16;

	FVector2f YawPitchDeltas[Capacity];
	int32     WriteIndex = 0;
	int32     Count      = 0;

	FMeleeAttackDirectionBuffer()
	{
		for (int32 i = 0; i < Capacity; ++i) YawPitchDeltas[i] = FVector2f::ZeroVector;
	}

	/** Insert one (yawDelta, pitchDelta) sample. Ring — oldest evicted when full. */
	void Push(FVector2f Sample);

	/** Classify accumulated motion into a swing direction.
	 *  WindowSeconds is an approximation: MVP sums the last-N entries (N derived from
	 *  expected input rate). Proper wall-clock windowing needs per-sample timestamps —
	 *  flagged as TODO in the .cpp; blueprint §A.1 does not prescribe either approach. */
	EMeleeSwingDirection Resolve(float WindowSeconds) const;
};

/** Sim-side blade socket snapshot. Read by MeleeSweepSystem every tick.
 *  For simple-AI weapons (BladeBuffer == nullptr) the AI driver writes here
 *  directly; for player/full-AI the triple-buffer drain copies into a local
 *  snapshot instead (this component is unused on that path). N-C3. */
USTRUCT()
struct FBladeSocketSync
{
	GENERATED_BODY()

	FVector BladeStartWS = FVector::ZeroVector;
	FVector BladeTipWS   = FVector::ZeroVector;
	/** 0 ⇒ never written; sim uses this to detect stale first-tick. */
	uint64  FrameStamp   = 0;
};

/** Accumulator component on DEFENDER — multiple attackers in one tick merge here.
 *  Sum stamina cost, max absorb fraction, "any perfect" → Perfect (N-C1). */
USTRUCT()
struct FPendingBlockAbsorb
{
	GENERATED_BODY()

	float               TotalStaminaCost  = 0.f;
	float               MaxAbsorbFraction = 0.f;
	EBlockTimingQuality TimingQuality     = EBlockTimingQuality::Normal;
	int32               AbsorbCount       = 0;

	void AddAbsorb(float InStamina, float InAbsorb, EBlockTimingQuality InTiming);
};

// ═══════════════════════════════════════════════════════════════
// TAGS (Flecs components — zero size)
// ═══════════════════════════════════════════════════════════════

/** Entity is a melee weapon. */
USTRUCT() struct FTagMeleeWeapon { GENERATED_BODY() };

/** Weapon phase ∈ {Windup, Release, Recovery} — query filter for active swings. */
USTRUCT() struct FTagMeleeAttacking { GENERATED_BODY() };

/** Weapon is currently accumulating charge. NOTE: distinct from ranged FTagChargingWeapon. */
USTRUCT() struct FTagMeleeCharging { GENERATED_BODY() };

/** Lives on CHARACTER, not weapon — character is holding guard. */
USTRUCT() struct FTagMeleeBlocking { GENERATED_BODY() };

// ═══════════════════════════════════════════════════════════════
// FORWARD-COMPAT STUBS
// ═══════════════════════════════════════════════════════════════

/** Bone reference stub — today returned as {0, NAME_None, 1.f} for single-body targets.
 *  Future skeleton work (§G.2) populates per-bone data without breaking callers. */
USTRUCT()
struct FBoneRef
{
	GENERATED_BODY()

	uint64 ParentRootEntityId = 0;
	FName  BoneName           = NAME_None;
	float  LocalImpulseScale  = 1.f;
};

/** Resolved hit target — root entity + optional bone info (§G.1).
 *  Plain USTRUCT; not currently a Flecs component (pass-by-value through libraries). */
USTRUCT()
struct FResolvedHitTarget
{
	GENERATED_BODY()

	/** Flecs entity ID of the health-bearing root entity. 0 = no resolution. */
	uint64   RootEntityId = 0;
	FBoneRef BoneInfo;
	bool     bIsBoneHit   = false;
};
