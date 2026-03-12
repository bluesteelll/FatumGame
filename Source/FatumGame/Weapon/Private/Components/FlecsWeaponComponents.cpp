// FWeaponStatic::FromProfile — converts UFlecsWeaponProfile (UObject) to sim-thread struct.

#include "FlecsWeaponComponents.h"
#include "FlecsWeaponProfile.h"

FWeaponStatic FWeaponStatic::FromProfile(const UFlecsWeaponProfile* Profile)
{
	check(Profile);

	FWeaponStatic S;

	// Firing
	S.ProjectileDefinition = Profile->ProjectileDefinition;
	S.FireInterval = Profile->GetFireInterval();
	S.BurstCount = Profile->BurstCount;
	S.BurstDelay = Profile->GetFireInterval() * 2.f;
	S.ProjectileSpeedMultiplier = Profile->ProjectileSpeedMultiplier;
	S.DamageMultiplier = Profile->DamageMultiplier;
	S.ProjectilesPerShot = Profile->ProjectilesPerShot;
	S.bIsAutomatic = Profile->IsAutomatic();
	S.bIsBurst = Profile->IsBurst();

	// Ammo
	S.MagazineSize = Profile->MagazineSize;
	S.ReloadTime = Profile->ReloadTime;
	S.MaxReserveAmmo = Profile->MaxReserveAmmo;
	S.AmmoPerShot = Profile->AmmoPerShot;
	S.bUnlimitedAmmo = Profile->HasUnlimitedAmmo();

	// Muzzle
	S.MuzzleOffset = Profile->MuzzleOffset;
	S.MuzzleSocketName = Profile->MuzzleSocketName;

	// Visuals
	S.EquippedMesh = Profile->EquippedMesh;
	S.DroppedMesh = Profile->DroppedMesh;
	S.AttachSocket = Profile->AttachSocket;
	S.AttachOffset = Profile->AttachOffset;
	S.DroppedScale = Profile->DroppedScale;

	// Animations
	S.FireMontage = Profile->FireMontage;
	S.ReloadMontage = Profile->ReloadMontage;
	S.EquipMontage = Profile->EquipMontage;

	// Bloom
	S.BaseSpread = Profile->BaseSpread;
	S.SpreadPerShot = Profile->SpreadPerShot;
	S.MaxSpread = Profile->MaxSpread;
	S.SpreadDecayRate = Profile->SpreadDecayRate;
	S.SpreadRecoveryDelay = Profile->SpreadRecoveryDelay;
	S.MovingSpreadAdd = Profile->MovingSpreadAdd;
	S.JumpingSpreadAdd = Profile->JumpingSpreadAdd;

	return S;
}
