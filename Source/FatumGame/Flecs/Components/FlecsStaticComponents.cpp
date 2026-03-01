// FromProfile() factory methods for static ECS components.
// Converts UDataAsset profiles to sim-thread structs (shared via prefab inheritance).

#include "FlecsStaticComponents.h"
#include "FlecsHealthProfile.h"
#include "FlecsDamageProfile.h"
#include "FlecsProjectileProfile.h"
#include "FlecsContainerProfile.h"
#include "FlecsItemDefinition.h"
#include "FlecsEntityDefinition.h"
#include "FlecsInteractionProfile.h"

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

// ═══════════════════════════════════════════════════════════════
// PROJECTILE
// ═══════════════════════════════════════════════════════════════

FProjectileStatic FProjectileStatic::FromProfile(const UFlecsProjectileProfile* Profile)
{
	check(Profile);

	FProjectileStatic S;
	S.MaxLifetime = Profile->Lifetime;
	S.MaxBounces = Profile->MaxBounces;
	S.GracePeriodFrames = Profile->GetGraceFrames();
	S.MinVelocity = Profile->MinVelocity;
	S.bMaintainSpeed = Profile->bMaintainSpeed;
	S.TargetSpeed = Profile->DefaultSpeed;
	return S;
}

// ═══════════════════════════════════════════════════════════════
// ITEM
// ═══════════════════════════════════════════════════════════════

FItemStaticData FItemStaticData::FromProfile(UFlecsItemDefinition* ItemDef, UFlecsEntityDefinition* EntityDef)
{
	check(ItemDef);

	FItemStaticData S;
	S.TypeId = ItemDef->ItemTypeId != 0 ? ItemDef->ItemTypeId : GetTypeHash(ItemDef->ItemName);
	S.MaxStack = ItemDef->MaxStackSize;
	S.Weight = ItemDef->Weight;
	S.GridSize = ItemDef->GridSize;
	S.ItemName = ItemDef->ItemName;
	S.EntityDefinition = EntityDef;
	S.ItemDefinition = ItemDef;
	return S;
}

// ═══════════════════════════════════════════════════════════════
// CONTAINER
// ═══════════════════════════════════════════════════════════════

FContainerStatic FContainerStatic::FromProfile(const UFlecsContainerProfile* Profile)
{
	check(Profile);

	FContainerStatic S;
	S.Type = Profile->ContainerType;
	S.GridWidth = Profile->GridWidth;
	S.GridHeight = Profile->GridHeight;
	S.MaxItems = Profile->MaxListItems;
	S.MaxWeight = Profile->MaxWeight;
	S.bAllowNesting = Profile->bAllowNestedContainers;
	S.bAutoStack = Profile->bAutoStackOnAdd;
	return S;
}

// ═══════════════════════════════════════════════════════════════
// INTERACTION
// ═══════════════════════════════════════════════════════════════

FInteractionStatic FInteractionStatic::FromProfile(const UFlecsInteractionProfile* Profile)
{
	check(Profile);

	FInteractionStatic S;
	S.MaxRange = Profile->InteractionRange;
	S.bSingleUse = Profile->bSingleUse;
	S.InteractionType = static_cast<uint8>(Profile->InteractionType);
	S.InstantAction = static_cast<uint8>(Profile->InstantAction);
	S.bRestrictAngle = Profile->bRestrictAngle;
	if (Profile->bRestrictAngle)
	{
		S.AngleCosine = FMath::Cos(FMath::DegreesToRadians(Profile->InteractionAngle));
		FVector Dir = Profile->InteractionDirection.GetSafeNormal();
		S.AngleDirection = Dir.IsNearlyZero() ? FVector::ForwardVector : Dir;
	}
	return S;
}
