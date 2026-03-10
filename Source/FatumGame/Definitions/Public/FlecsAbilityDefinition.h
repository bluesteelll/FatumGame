// Data Asset defining a single ability type with charge/recharge configuration.
// Referenced by UFlecsAbilityLoadout to build FAbilitySystem on entity creation.
// Per-ability-type config structs are conditionally visible based on AbilityType.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsAbilityTypes.h"
#include "FlecsResourcePoolProfile.h" // EResourceType, FAbilityCostDefinition
#include "FlecsAbilityDefinition.generated.h"

// Blueprint-exposed mirror of EAbilityTypeId. Values MUST match exactly.
// static_assert below enforces sync.
UENUM(BlueprintType)
enum class EAbilityType : uint8
{
	None = 0,
	Slide = 1,
	Blink = 2,
	Mantle = 3,
	KineticBlast = 4,
	Telekinesis = 5,
	Climb = 6,
	RopeSwing = 7,
	MAX UMETA(Hidden)
};

static_assert(static_cast<uint8>(EAbilityType::MAX) == static_cast<uint8>(EAbilityTypeId::MAX),
	"EAbilityType and EAbilityTypeId must stay in sync");

// ── Per-ability-type config structs ──

USTRUCT(BlueprintType)
struct FKineticBlastConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, meta = (ClampMin = "100", ClampMax = "5000"))
	float ConeRadius = 1500.f;           // cm, max reach

	UPROPERTY(EditAnywhere, meta = (ClampMin = "10", ClampMax = "90"))
	float ConeHalfAngle = 45.f;          // degrees (90° total cone)

	UPROPERTY(EditAnywhere, meta = (ClampMin = "100", ClampMax = "50000"))
	float ImpulseStrength = 5000.f;      // cm/s, base impulse magnitude

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "3"))
	float FalloffExponent = 1.f;         // 1.0 = linear, 0.0 = none

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0", ClampMax = "2"))
	float SelfImpulseMultiplier = 0.2f;  // self-knockback scale

	UPROPERTY(EditAnywhere)
	bool bAffectSelf = true;
};

static_assert(sizeof(FKineticBlastConfig) <= ABILITY_CONFIG_SIZE,
	"FKineticBlastConfig must fit in FAbilitySlot::ConfigData");
static_assert(alignof(FKineticBlastConfig) <= 8,
	"FKineticBlastConfig alignment exceeds ConfigData alignas(8)");

USTRUCT(BlueprintType)
struct FTelekinesisConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Grab", meta = (ClampMin = "100", ClampMax = "5000"))
	float MaxGrabRange = 1500.f;           // cm

	UPROPERTY(EditAnywhere, Category = "Hold", meta = (ClampMin = "50", ClampMax = "1000"))
	float HoldDistance = 300.f;            // cm, default hold distance

	UPROPERTY(EditAnywhere, Category = "Hold", meta = (ClampMin = "50", ClampMax = "500"))
	float MinHoldDistance = 150.f;         // cm

	UPROPERTY(EditAnywhere, Category = "Hold", meta = (ClampMin = "200", ClampMax = "2000"))
	float MaxHoldDistance = 600.f;         // cm

	UPROPERTY(EditAnywhere, Category = "Hold", meta = (ClampMin = "10", ClampMax = "200"))
	float ScrollSpeed = 50.f;             // cm per scroll tick

	UPROPERTY(EditAnywhere, Category = "Hold", meta = (ClampMin = "-100", ClampMax = "100"))
	float VerticalHoldOffset = -30.f;     // cm below crosshair

	UPROPERTY(EditAnywhere, Category = "Grab", meta = (ClampMin = "1", ClampMax = "10000"))
	float MaxGrabbableMass = 200.f;       // kg

	UPROPERTY(EditAnywhere, Category = "Physics", meta = (ClampMin = "0", ClampMax = "20"))
	float AngularDamping = 5.f;           // s^-1, damps spinning while held

	UPROPERTY(EditAnywhere, Category = "Mana", meta = (ClampMin = "0.1", ClampMax = "50"))
	float BaseDrainRate = 8.f;            // mana/sec at reference mass

	UPROPERTY(EditAnywhere, Category = "Mana", meta = (ClampMin = "0.1", ClampMax = "100"))
	float ReferenceMass = 10.f;           // kg, mass at which multiplier = 1.0

	UPROPERTY(EditAnywhere, Category = "Mana", meta = (ClampMin = "0.1", ClampMax = "2"))
	float MassExponent = 0.5f;            // sqrt: lighter = cheaper, heavier = costlier

	UPROPERTY(EditAnywhere, Category = "Throw", meta = (ClampMin = "500", ClampMax = "10000"))
	float ThrowSpeed = 3000.f;            // cm/s

	UPROPERTY(EditAnywhere, Category = "Safety", meta = (ClampMin = "0.5", ClampMax = "10"))
	float MaxStuckTime = 2.f;             // seconds before auto-release

	UPROPERTY(EditAnywhere, Category = "Safety", meta = (ClampMin = "0.5", ClampMax = "5"))
	float AcquireTimeout = 1.f;           // seconds to reach hold point

	UPROPERTY(EditAnywhere, Category = "Physics", meta = (ClampMin = "1", ClampMax = "100"))
	float HoldPointInterpSpeed = 20.f;    // higher = more responsive, lower = smoother (prevents pivot jitter)
};

// FTelekinesisConfig lives on the DA (any size). Only mutable runtime data goes into ConfigData.
// Also caches a const pointer to the DA config for sim-thread read access.
struct FTelekinesisSlotData
{
	const FTelekinesisConfig* Config = nullptr; // 8 bytes — immutable DA pointer
	float CurrentHoldDistance = 300.f;
	float MinHoldDistance = 150.f;
	float MaxHoldDistance = 600.f;
	float ScrollSpeed = 50.f;
	// = 24 bytes total
};

static_assert(sizeof(FTelekinesisSlotData) <= ABILITY_CONFIG_SIZE,
	"FTelekinesisSlotData must fit in FAbilitySlot::ConfigData");
static_assert(alignof(FTelekinesisSlotData) <= 8,
	"FTelekinesisSlotData alignment exceeds ConfigData alignas(8)");

// ── Ability Definition Data Asset ──

UCLASS(BlueprintType)
class FATUMGAME_API UFlecsAbilityDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Identity")
	FName AbilityName = "NewAbility";

	UPROPERTY(EditAnywhere, Category = "Identity")
	EAbilityType AbilityType = EAbilityType::None;

	/** -1 = infinite charges */
	UPROPERTY(EditAnywhere, Category = "Charges", meta = (ClampMin = "-1", ClampMax = "10"))
	int32 StartingCharges = -1;

	UPROPERTY(EditAnywhere, Category = "Charges", meta = (ClampMin = "1", ClampMax = "10"))
	int32 MaxCharges = 1;

	/** Seconds per charge recharge. 0 = no recharge. */
	UPROPERTY(EditAnywhere, Category = "Charges", meta = (ClampMin = "0", ClampMax = "60"))
	float RechargeRate = 0.f;

	/** Per-use cooldown (seconds). Blocks re-activation after ability ends. 0 = no cooldown. */
	UPROPERTY(EditAnywhere, Category = "Charges", meta = (ClampMin = "0", ClampMax = "30"))
	float CooldownDuration = 0.f;

	/** Tick even when inactive (Phase==0). Use for always-on abilities like Blink. */
	UPROPERTY(EditAnywhere, Category = "Behavior")
	bool bAlwaysTick = false;

	/** When active, only this ability ticks (suppress all others). Use for Mantle. */
	UPROPERTY(EditAnywhere, Category = "Behavior")
	bool bExclusive = false;

	// ── Resource Costs ──

	/** One-time costs consumed on activation. Max 2 entries. */
	UPROPERTY(EditAnywhere, Category = "Resource Costs", meta = (TitleProperty = "ResourceType"))
	TArray<FAbilityCostDefinition> ActivationCosts;

	/** Per-second costs while ability is active (channeled abilities). Max 2 entries. */
	UPROPERTY(EditAnywhere, Category = "Resource Costs", meta = (TitleProperty = "ResourceType"))
	TArray<FAbilityCostDefinition> SustainCosts;

	/** Fraction (0.0-1.0) of activation costs refunded on voluntary deactivation (End). */
	UPROPERTY(EditAnywhere, Category = "Resource Costs", meta = (ClampMin = "0", ClampMax = "1"))
	float DeactivationRefund = 0.f;

	// ── Per-ability-type configs (conditionally visible) ──

	UPROPERTY(EditAnywhere, Category = "KineticBlast",
		meta = (EditCondition = "bIsKineticBlast", EditConditionHides))
	FKineticBlastConfig KineticBlastConfig;

	UPROPERTY(EditAnywhere, Category = "Telekinesis",
		meta = (EditCondition = "bIsTelekinesis", EditConditionHides))
	FTelekinesisConfig TelekinesisConfig;

#if WITH_EDITORONLY_DATA
private:
	// Helper bools for EditCondition (enum == comparison is unreliable in UE meta).
	// NOT serialized — computed from AbilityType via PostEditChangeProperty.

	UPROPERTY(Transient, meta = (HideInDetailPanel))
	bool bIsKineticBlast = false;

	UPROPERTY(Transient, meta = (HideInDetailPanel))
	bool bIsTelekinesis = false;

public:
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;

private:
	void RefreshEditorBools();
#endif
};
