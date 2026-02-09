
#include "FlecsWeaponLibrary.h"
#include "FlecsLibraryHelpers.h"
#include "FlecsWeaponComponents.h"

// ═══════════════════════════════════════════════════════════════
// WEAPON CONTROL
// ═══════════════════════════════════════════════════════════════

void UFlecsWeaponLibrary::StartFiring(UObject* WorldContextObject, int64 WeaponEntityId)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || WeaponEntityId == 0) return;

	Subsystem->EnqueueCommand([Subsystem, WeaponEntityId]()
	{
		flecs::world* World = Subsystem->GetFlecsWorld();
		if (!World) return;

		flecs::entity WeaponEntity = World->entity(static_cast<flecs::entity_t>(WeaponEntityId));
		if (!WeaponEntity.is_valid() || !WeaponEntity.is_alive()) return;

		FWeaponInstance* Weapon = WeaponEntity.try_get_mut<FWeaponInstance>();
		if (Weapon)
		{
			Weapon->bFireRequested = true;
			Weapon->bFireTriggerPending = true;
		}
	});
}

void UFlecsWeaponLibrary::StopFiring(UObject* WorldContextObject, int64 WeaponEntityId)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || WeaponEntityId == 0) return;

	Subsystem->EnqueueCommand([Subsystem, WeaponEntityId]()
	{
		flecs::world* World = Subsystem->GetFlecsWorld();
		if (!World) return;

		flecs::entity WeaponEntity = World->entity(static_cast<flecs::entity_t>(WeaponEntityId));
		if (!WeaponEntity.is_valid() || !WeaponEntity.is_alive()) return;

		FWeaponInstance* Weapon = WeaponEntity.try_get_mut<FWeaponInstance>();
		if (Weapon)
		{
			Weapon->bFireRequested = false;
		}
	});
}

void UFlecsWeaponLibrary::ReloadWeapon(UObject* WorldContextObject, int64 WeaponEntityId)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || WeaponEntityId == 0) return;

	Subsystem->EnqueueCommand([Subsystem, WeaponEntityId]()
	{
		flecs::world* World = Subsystem->GetFlecsWorld();
		if (!World) return;

		flecs::entity WeaponEntity = World->entity(static_cast<flecs::entity_t>(WeaponEntityId));
		if (!WeaponEntity.is_valid() || !WeaponEntity.is_alive()) return;

		FWeaponInstance* Weapon = WeaponEntity.try_get_mut<FWeaponInstance>();
		if (Weapon)
		{
			Weapon->bReloadRequested = true;
		}
	});
}

void UFlecsWeaponLibrary::SetAimDirection(UObject* WorldContextObject, int64 CharacterEntityId, FVector Direction, FVector CharacterPosition)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || CharacterEntityId == 0) return;

	FVector NormalizedDir = Direction.GetSafeNormal();
	if (NormalizedDir.IsNearlyZero())
	{
		NormalizedDir = FVector::ForwardVector;
	}

	Subsystem->EnqueueCommand([Subsystem, CharacterEntityId, NormalizedDir, CharacterPosition]()
	{
		flecs::world* World = Subsystem->GetFlecsWorld();
		if (!World) return;

		flecs::entity CharEntity = World->entity(static_cast<flecs::entity_t>(CharacterEntityId));
		if (!CharEntity.is_valid() || !CharEntity.is_alive()) return;

		FAimDirection AimDir;
		AimDir.Direction = NormalizedDir;
		AimDir.CharacterPosition = CharacterPosition;
		CharEntity.set<FAimDirection>(AimDir);
	});
}

int32 UFlecsWeaponLibrary::GetWeaponAmmo(UObject* WorldContextObject, int64 WeaponEntityId)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || WeaponEntityId == 0) return -1;

	flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
	if (!FlecsWorld) return -1;

	flecs::entity WeaponEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(WeaponEntityId));
	if (!WeaponEntity.is_valid() || !WeaponEntity.is_alive()) return -1;

	const FWeaponInstance* Weapon = WeaponEntity.try_get<FWeaponInstance>();
	if (!Weapon) return -1;

	return Weapon->CurrentAmmo;
}

bool UFlecsWeaponLibrary::GetWeaponAmmoInfo(UObject* WorldContextObject, int64 WeaponEntityId,
	int32& OutCurrentAmmo, int32& OutMagazineSize, int32& OutReserveAmmo)
{
	OutCurrentAmmo = 0;
	OutMagazineSize = 0;
	OutReserveAmmo = 0;

	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || WeaponEntityId == 0) return false;

	flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
	if (!FlecsWorld) return false;

	flecs::entity WeaponEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(WeaponEntityId));
	if (!WeaponEntity.is_valid() || !WeaponEntity.is_alive()) return false;

	const FWeaponInstance* Instance = WeaponEntity.try_get<FWeaponInstance>();
	const FWeaponStatic* Static = WeaponEntity.try_get<FWeaponStatic>();
	if (!Instance) return false;

	OutCurrentAmmo = Instance->CurrentAmmo;
	OutReserveAmmo = Instance->ReserveAmmo;
	OutMagazineSize = Static ? Static->MagazineSize : 0;

	return true;
}

bool UFlecsWeaponLibrary::IsWeaponReloading(UObject* WorldContextObject, int64 WeaponEntityId)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || WeaponEntityId == 0) return false;

	flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
	if (!FlecsWorld) return false;

	flecs::entity WeaponEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(WeaponEntityId));
	if (!WeaponEntity.is_valid() || !WeaponEntity.is_alive()) return false;

	const FWeaponInstance* Weapon = WeaponEntity.try_get<FWeaponInstance>();
	if (!Weapon) return false;

	return Weapon->bIsReloading;
}
