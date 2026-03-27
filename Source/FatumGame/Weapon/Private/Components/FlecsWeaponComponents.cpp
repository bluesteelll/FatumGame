// FWeaponStatic::FromProfile — converts UFlecsWeaponProfile (UObject) to sim-thread struct.

#include "FlecsWeaponComponents.h"
#include "FlecsWeaponProfile.h"
#include "FlecsCaliberRegistry.h"

FWeaponStatic FWeaponStatic::FromProfile(const UFlecsWeaponProfile* Profile, const UFlecsCaliberRegistry* CaliberRegistry)
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

	// Ammo & Reload — resolve caliber names to uint8 IDs via registry
	S.AcceptedCaliberCount = FMath::Min(Profile->AcceptedCalibers.Num(), FWeaponStatic::MaxAcceptedCalibers);
	for (int32 i = 0; i < S.AcceptedCaliberCount; ++i)
	{
		S.AcceptedCaliberIds[i] = CaliberRegistry ? CaliberRegistry->GetCaliberIndex(Profile->AcceptedCalibers[i]) : 0xFF;
		checkf(S.AcceptedCaliberIds[i] != 0xFF, TEXT("Weapon caliber '%s' not found in CaliberRegistry"), *Profile->AcceptedCalibers[i].ToString());
	}
	S.AmmoPerShot = Profile->AmmoPerShot;
	S.bHasChamber = Profile->bHasChamber;
	S.bUnlimitedAmmo = Profile->HasUnlimitedAmmo();
	S.RemoveMagTime = Profile->RemoveMagTime;
	S.InsertMagTime = Profile->InsertMagTime;
	S.ChamberTime = Profile->ChamberTime;
	S.ReloadMoveSpeedMultiplier = Profile->ReloadMoveSpeedMultiplier;

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
	S.EquipTime = Profile->EquipTime;

	// Trigger Pull
	S.bEnableTriggerPull = Profile->bEnableTriggerPull;
	S.TriggerPullTime = Profile->TriggerPullTime;
	S.bTriggerPullEveryShot = Profile->bTriggerPullEveryShot;

	// Reload Type
	S.ReloadType = static_cast<uint8>(Profile->ReloadType);
	S.OpenTime = Profile->OpenTime;
	S.InsertRoundTime = Profile->InsertRoundTime;
	S.CloseTime = Profile->CloseTime;

	// Post-Fire Cycling
	S.bRequiresCycling = Profile->bRequiresCycling;
	S.CycleTime = Profile->CycleTime;

	// Spread & Bloom (decidegrees)
	S.BaseSpread = Profile->BaseSpread;
	S.SpreadPerShot = Profile->SpreadPerShot;
	S.MaxBloom = Profile->MaxBloom;
	S.BloomDecayRate = Profile->BloomDecayRate;
	S.BloomRecoveryDelay = Profile->BloomRecoveryDelay;

	// Movement state multipliers (base spread + bloom)
	static_assert(static_cast<uint8>(EWeaponMoveState::MAX) == FWeaponStatic::NumMoveStates,
		"Multiplier array size must match EWeaponMoveState count");
	for (uint8 i = 0; i < FWeaponStatic::NumMoveStates; ++i)
	{
		const EWeaponMoveState State = static_cast<EWeaponMoveState>(i);
		const FWeaponStateMultipliers& M = Profile->GetStateMultipliers(State);
		S.BaseSpreadMultipliers[i] = M.SpreadBaseMultiplier;
		S.BloomMultipliers[i] = M.BloomMultiplier;
	}

	// Pellet ring spread (Technique G)
	S.PelletRingCount = FMath::Min(Profile->PelletRings.Num(), FWeaponStatic::MaxPelletRings);
	for (int32 i = 0; i < S.PelletRingCount; ++i)
	{
		S.PelletRings[i].PelletCount = Profile->PelletRings[i].PelletCount;
		S.PelletRings[i].RadiusRadians = FMath::DegreesToRadians(Profile->PelletRings[i].RadiusDecidegrees * 0.1f);
		S.PelletRings[i].AngularJitterRadians = FMath::DegreesToRadians(Profile->PelletRings[i].AngularJitterDecidegrees * 0.1f);
		S.PelletRings[i].RadialJitterRadians = FMath::DegreesToRadians(Profile->PelletRings[i].RadialJitterDecidegrees * 0.1f);
	}
	// If rings defined, derive ProjectilesPerShot from ring total (rings are authoritative)
	if (S.PelletRingCount > 0)
	{
		int32 Total = 0;
		for (int32 i = 0; i < S.PelletRingCount; ++i)
			Total += S.PelletRings[i].PelletCount;
		S.ProjectilesPerShot = Total;
	}

	return S;
}
