// FromProfile() factory methods for health/damage static components.

#include "FlecsHealthComponents.h"
#include "FlecsHealthProfile.h"
#include "FlecsDamageProfile.h"
#include "Properties/FlecsComponentProperties.h"

REGISTER_FLECS_COMPONENT(FHealthInstance);

// ═══════════════════════════════════════════════════════════════
// HEALTH
// ═══════════════════════════════════════════════════════════════

FHealthStatic FHealthStatic::FromProfile(const UFlecsHealthProfile* Profile)
{
	check(Profile);

	FHealthStatic S;
	S.MaxHP = Profile->MaxHealth;
	S.Armor = Profile->Armor;
	S.RegenPerSecond = Profile->RegenPerSecond;
	S.bDestroyOnDeath = Profile->bDestroyOnDeath;
	S.StartingHPRatio = Profile->StartingHealth > 0.f
		? Profile->StartingHealth / Profile->MaxHealth
		: 1.f;
	return S;
}

// ═══════════════════════════════════════════════════════════════
// DAMAGE
// ═══════════════════════════════════════════════════════════════

FDamageStatic FDamageStatic::FromProfile(const UFlecsDamageProfile* Profile)
{
	check(Profile);

	FDamageStatic S;
	S.Damage = Profile->Damage;
	S.DamageType = Profile->DamageType;
	S.bAreaDamage = Profile->bAreaDamage;
	S.AreaRadius = Profile->AreaRadius;
	S.bDestroyOnHit = Profile->bDestroyOnHit;
	S.CritChance = Profile->CriticalChance;
	S.CritMultiplier = Profile->CriticalMultiplier;
	return S;
}
