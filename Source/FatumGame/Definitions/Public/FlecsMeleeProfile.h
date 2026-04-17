// Melee weapon profile — UDataAsset mirroring FMeleeWeaponStatic field-for-field.
// Blueprint reference: Melee Combat System Rev 3, §D.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsMeleeComponents.h" // EMeleeDamageDelivery enum
#include "FlecsMeleeProfile.generated.h"

class UCurveFloat;
class UNiagaraSystem;
class USkeletalMesh;

/**
 * Melee weapon profile - defines swing behavior, damage, charge, block, and penetration.
 *
 * Usage:
 * - Create UFlecsEntityDefinition referencing a UFlecsMeleeProfile
 * - Equip-system builds FMeleeWeaponStatic via FromProfile at equip time
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsMeleeProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// VISUALS
	// ═══════════════════════════════════════════════════════════════

	/** Skeletal mesh applied to the character's WeaponMeshComponent on equip.
	 *  MUST contain the BladeStartSocket / BladeTipSocket sockets below —
	 *  the sweep reads their world transforms every Tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Equipped")
	TObjectPtr<USkeletalMesh> EquippedMesh;

	/** Local transform applied to the weapon mesh on equip (relative to WeaponMeshComponent's
	 *  parent, typically FollowCamera). Identity = mesh pivot sits at the camera origin which
	 *  is usually invisible in 1P. Designer tunes this to place the weapon in front-and-below
	 *  the camera view. Mirrors UFlecsWeaponProfile::AttachOffset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Equipped")
	FTransform AttachOffset;

	// ═══════════════════════════════════════════════════════════════
	// GEOMETRY
	// ═══════════════════════════════════════════════════════════════

	/** Hilt-end socket on the skeletal mesh (capsule start). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	FName BladeStartSocket = TEXT("hilt_base");

	/** Tip socket on the skeletal mesh (capsule end). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	FName BladeTipSocket = TEXT("blade_tip");

	/** Effective blade length in cm (used for range checks and future tuning). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ClampMin = "10", ClampMax = "400"))
	float Reach = 120.f;

	/** Capsule radius in cm used by the sweep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ClampMin = "0.5", ClampMax = "20"))
	float CapsuleRadius = 4.f;

	/** Retained for future ragdoll tuning; NOT used in impulse formula. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ClampMin = "0.1"))
	float WeaponMass = 1.5f;

	/** Number of slerped capsule substeps per sim tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ClampMin = "1", ClampMax = "8"))
	int32 SweepSubstepsPerTick = 3;

	// ═══════════════════════════════════════════════════════════════
	// PHASE TIMINGS (seconds)
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phases", meta = (ClampMin = "0.01", ClampMax = "2.0"))
	float WindupTime = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phases", meta = (ClampMin = "0.01", ClampMax = "2.0"))
	float ReleaseTime = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phases", meta = (ClampMin = "0.01", ClampMax = "3.0"))
	float RecoveryTime = 0.40f;

	// ═══════════════════════════════════════════════════════════════
	// DAMAGE
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0"))
	float BaseDamage = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0"))
	float BaseImpulse = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0", ClampMax = "1"))
	float CritChance = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "1"))
	float CritMultiplier = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	EMeleeDamageDelivery DeliveryType = EMeleeDamageDelivery::Slashing;

	/** Reference tip speed (cm/s) at which the speed multiplier is 1.0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "1"))
	float ReferenceTipSpeed = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0"))
	float TipSpeedMulMin = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0"))
	float TipSpeedMulMax = 1.4f;

	// ═══════════════════════════════════════════════════════════════
	// PENETRATION (inline MVP per blueprint §J Q8)
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0"))
	float PenetrationBudget = 20.f;

	/** -1 = unlimited penetrations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "-1"))
	int32 MaxPenetrations = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "2"))
	float PenetrationDamageFalloff = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "1"))
	float PenetrationVelocityFalloff = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "1"))
	float PenetrationImpulseTransfer = 0.3f;

	/** Max angle (degrees from surface normal) at which penetration is attempted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penetration", meta = (ClampMin = "0", ClampMax = "89"))
	float PenetrationRicochetAngleDeg = 70.f;

	// ═══════════════════════════════════════════════════════════════
	// CHARGE
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge")
	bool bEnableCharge = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge",
		meta = (ClampMin = "0.01", EditCondition = "bEnableCharge", EditConditionHides))
	float MinChargeTime = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge",
		meta = (ClampMin = "0.02", EditCondition = "bEnableCharge", EditConditionHides))
	float MaxChargeTime = 0.90f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge",
		meta = (EditCondition = "bEnableCharge", EditConditionHides))
	bool bAutoFireAtMaxCharge = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge",
		meta = (EditCondition = "bEnableCharge && bAutoFireAtMaxCharge", EditConditionHides))
	bool bAutoRestartCharge = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge",
		meta = (EditCondition = "bEnableCharge", EditConditionHides))
	TObjectPtr<UCurveFloat> ChargeCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge",
		meta = (ClampMin = "0.1", EditCondition = "bEnableCharge", EditConditionHides))
	float DamageMaxMultiplier = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge",
		meta = (ClampMin = "0.1", EditCondition = "bEnableCharge", EditConditionHides))
	float ImpulseMaxMultiplier = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge",
		meta = (ClampMin = "0.1", EditCondition = "bEnableCharge", EditConditionHides))
	float PenetrationMaxMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge",
		meta = (ClampMin = "0.1", EditCondition = "bEnableCharge", EditConditionHides))
	float SwingSpeedMaxMultiplier = 1.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Charge",
		meta = (ClampMin = "0.1", EditCondition = "bEnableCharge", EditConditionHides))
	float StaminaCostMaxMultiplier = 2.0f;

	// ═══════════════════════════════════════════════════════════════
	// STAMINA
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stamina", meta = (ClampMin = "0"))
	float StaminaCostPerSwing = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stamina", meta = (ClampMin = "0"))
	float BaseBlockStaminaCost = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stamina", meta = (ClampMin = "0"))
	float StaminaCostPerBlockSecond = 5.f;

	// ═══════════════════════════════════════════════════════════════
	// BLOCK — continuous quality (Q8)
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	bool bCanBlock = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block",
		meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bCanBlock", EditConditionHides))
	float MinBlockAbsorb = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block",
		meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bCanBlock", EditConditionHides))
	float MaxBlockAbsorb = 0.95f;

	/** Stamina-cost discount applied when the defender faces the attacker head-on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block",
		meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bCanBlock", EditConditionHides))
	float DirectionalDiscountFactor = 0.50f;

	/** Window after block start during which TimingQuality == Perfect (real seconds, N-M2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block",
		meta = (ClampMin = "0.01", ClampMax = "1.0", EditCondition = "bCanBlock", EditConditionHides))
	float PerfectBlockWindowSeconds = 0.15f;

	/** Stamina-cost multiplier on perfect-timing blocks. < 1 rewards the defender. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block",
		meta = (ClampMin = "0", ClampMax = "2", EditCondition = "bCanBlock", EditConditionHides))
	float PerfectTimingStaminaMultiplier = 0.5f;

	// ═══════════════════════════════════════════════════════════════
	// REBOUND
	// ═══════════════════════════════════════════════════════════════

	/** Recovery extension applied once per swing on Slashing-vs-hard-material rebound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rebound", meta = (ClampMin = "1", ClampMax = "4"))
	float ReboundRecoveryMultiplier = 1.0f;

	// ═══════════════════════════════════════════════════════════════
	// VFX
	// ═══════════════════════════════════════════════════════════════

	/** Blade trail Niagara system — parented to weapon socket during Release phase.
	 *  NOT compatible with the tracer-pool contract; uses EnqueueBladeTrail path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<UNiagaraSystem> TrailEffect = nullptr;

	/** Optional per-weapon impact VFX override. If null, uses the bullet surface-table fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<UNiagaraSystem> ImpactEffectOverride = nullptr;
};
