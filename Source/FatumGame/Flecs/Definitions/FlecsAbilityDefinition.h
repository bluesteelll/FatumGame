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

#if WITH_EDITORONLY_DATA
private:
	// Helper bools for EditCondition (enum == comparison is unreliable in UE meta).
	// NOT serialized — computed from AbilityType via PostEditChangeProperty.

	UPROPERTY(Transient, meta = (HideInDetailPanel))
	bool bIsKineticBlast = false;

public:
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;

private:
	void RefreshEditorBools();
#endif
};
