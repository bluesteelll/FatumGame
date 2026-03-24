// Weapon system setup dispatcher — delegates to per-system files.

#include "FlecsArtillerySubsystem.h"

void UFlecsArtillerySubsystem::SetupWeaponSystems()
{
	SetupWeaponTickSystem();
	SetupWeaponReloadSystem();
	SetupWeaponFireSystem();
}
