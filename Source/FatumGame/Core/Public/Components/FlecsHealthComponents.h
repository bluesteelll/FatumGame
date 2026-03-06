// Health and Damage components for Flecs Prefabs and Entities.
// Static data lives in PREFAB (shared), Instance data lives per-entity (mutable).

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "FlecsHealthComponents.generated.h"

class UFlecsHealthProfile;
class UFlecsDamageProfile;

// ═══════════════════════════════════════════════════════════════
// HEALTH STATIC
// ═══════════════════════════════════════════════════════════════

/**
 * Static health data - lives in PREFAB, shared by all entities of this type.
 * Contains immutable health rules.
 *
 * Instance data (CurrentHP) is in FHealthInstance.
 */
struct FHealthStatic
{
	/** Maximum hit points */
	float MaxHP = 100.f;

	/** Damage reduction (flat) */
	float Armor = 0.f;

	/** HP regeneration per second (0 = no regen) */
	float RegenPerSecond = 0.f;

	/** Should entity be destroyed when HP reaches 0? */
	bool bDestroyOnDeath = true;

	/** Starting HP ratio (1.0 = full health) */
	float StartingHPRatio = 1.f;

	float GetStartingHP() const { return MaxHP * StartingHPRatio; }

	static FHealthStatic FromProfile(const UFlecsHealthProfile* Profile);
};

// ═══════════════════════════════════════════════════════════════
// DAMAGE STATIC
// ═══════════════════════════════════════════════════════════════

/**
 * Static damage data - lives in PREFAB, shared by all entities of this type.
 * Contains immutable damage rules. No instance component needed.
 */
struct FDamageStatic
{
	/** Base damage dealt on contact */
	float Damage = 10.f;

	/** Damage type tag for resistance/weakness */
	FGameplayTag DamageType;

	/** Is this area damage? */
	bool bAreaDamage = false;

	/** Area damage radius (only if bAreaDamage) */
	float AreaRadius = 0.f;

	/** Should the entity be destroyed after dealing damage? */
	bool bDestroyOnHit = false;

	/** Critical hit chance (0.0 - 1.0) */
	float CritChance = 0.f;

	/** Critical hit damage multiplier */
	float CritMultiplier = 2.f;

	static FDamageStatic FromProfile(const UFlecsDamageProfile* Profile);
};

// ═══════════════════════════════════════════════════════════════
// HEALTH INSTANCE
// ═══════════════════════════════════════════════════════════════

/**
 * Instance health data - mutable per-entity data.
 * Static data (MaxHP, Armor) comes from FHealthStatic in prefab.
 */
USTRUCT(BlueprintType)
struct FHealthInstance
{
	GENERATED_BODY()

	/** Current hit points */
	UPROPERTY(BlueprintReadWrite, Category = "Health")
	float CurrentHP = 100.f;

	/** Accumulated regen (for fractional regen per tick) */
	float RegenAccumulator = 0.f;

	bool IsAlive() const { return CurrentHP > 0.f; }
};

// ═══════════════════════════════════════════════════════════════
// DAMAGE EVENT SYSTEM
// ═══════════════════════════════════════════════════════════════

/**
 * Single damage hit data.
 * NOT a USTRUCT - pure C++ for Flecs performance.
 */
struct FDamageHit
{
	/** Base damage before armor/modifiers */
	float Damage = 0.f;

	/** Source entity ID (0 = environment/no source) */
	uint64 SourceEntityId = 0;

	/** Damage type for resistances/weaknesses */
	FGameplayTag DamageType;

	/** World location of hit (for effects, knockback direction) */
	FVector HitLocation = FVector::ZeroVector;

	/** Was this a critical hit? */
	bool bIsCritical = false;

	/** Bypass armor calculation? */
	bool bIgnoreArmor = false;
};

/**
 * Pending damage component - accumulates hits for processing.
 * Added to entity when damage is queued, processed by DamageObserver.
 * NOT a USTRUCT - pure C++ for Flecs performance.
 */
struct FPendingDamage
{
	/** Accumulated damage hits to process */
	TArray<FDamageHit> Hits;

	/** Add a hit to pending queue */
	void AddHit(float Damage, uint64 SourceId = 0, FGameplayTag DamageType = FGameplayTag(),
	            FVector HitLocation = FVector::ZeroVector, bool bCritical = false, bool bIgnoreArmor = false)
	{
		FDamageHit Hit;
		Hit.Damage = Damage;
		Hit.SourceEntityId = SourceId;
		Hit.DamageType = DamageType;
		Hit.HitLocation = HitLocation;
		Hit.bIsCritical = bCritical;
		Hit.bIgnoreArmor = bIgnoreArmor;
		Hits.Add(Hit);
	}

	/** Clear all pending hits (keeps array capacity) */
	void Clear() { Hits.Reset(); }

	/** Check if there are pending hits */
	bool HasPendingDamage() const { return Hits.Num() > 0; }

	/** Get total pending damage (before armor) */
	float GetTotalPendingDamage() const
	{
		float Total = 0.f;
		for (const FDamageHit& Hit : Hits)
		{
			Total += Hit.Damage;
		}
		return Total;
	}
};
