// Weapon system setup dispatcher — delegates to per-system files.

#include "FlecsArtillerySubsystem.h"

void UFlecsArtillerySubsystem::SetupWeaponSystems()
{
	SetupWeaponEquipSystem();
	SetupWeaponChargeSystem();
	SetupWeaponTickSystem();
	SetupWeaponReloadSystem();
	SetupWeaponFireSystem();

	// MeleeInputResolveSystem not needed: input flows through UFlecsMeleeLibrary
	// → CommandQueue → direct writes to FMeleeWeaponInstance.bAttackRequested /
	//   bBlockRequested. Direction buffer is pumped from AFlecsCharacter::Look.
	SetupMeleeChargeSystem();
	SetupMeleeSwingInitSystem();
	SetupMeleePhaseAdvanceSystem();
	SetupMeleeSweepSystem();
}
