// Blueprint function library for Flecs ECS melee weapon control.
// Mirrors UFlecsWeaponLibrary (ranged) — same GetSubsystem + EnqueueCommand +
// try_get_mut pattern. Sim-thread-owned mutations on FMeleeWeaponInstance.

#include "FlecsMeleeLibrary.h"
#include "FlecsLibraryHelpers.h"
#include "FlecsMeleeComponents.h"

// ═══════════════════════════════════════════════════════════════
// MELEE CONTROL
// ═══════════════════════════════════════════════════════════════

void UFlecsMeleeLibrary::SetMeleeAttackRequested(UObject* WorldContextObject, int64 WeaponEntityId, bool bRequested)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || WeaponEntityId == 0) return;

	Subsystem->EnqueueCommand([Subsystem, WeaponEntityId, bRequested]()
	{
		flecs::world* World = Subsystem->GetFlecsWorld();
		if (!World) return;

		flecs::entity WeaponEntity = World->entity(static_cast<flecs::entity_t>(WeaponEntityId));
		if (!WeaponEntity.is_valid() || !WeaponEntity.is_alive()) return;

		FMeleeWeaponInstance* Inst = WeaponEntity.try_get_mut<FMeleeWeaponInstance>();
		if (Inst)
		{
			Inst->bAttackRequested = bRequested;
		}
	});
}

void UFlecsMeleeLibrary::SetMeleeBlockRequested(UObject* WorldContextObject, int64 WeaponEntityId, bool bRequested)
{
	UFlecsArtillerySubsystem* Subsystem = FlecsLibrary::GetSubsystem(WorldContextObject);
	if (!Subsystem || WeaponEntityId == 0) return;

	Subsystem->EnqueueCommand([Subsystem, WeaponEntityId, bRequested]()
	{
		flecs::world* World = Subsystem->GetFlecsWorld();
		if (!World) return;

		flecs::entity WeaponEntity = World->entity(static_cast<flecs::entity_t>(WeaponEntityId));
		if (!WeaponEntity.is_valid() || !WeaponEntity.is_alive()) return;

		FMeleeWeaponInstance* Inst = WeaponEntity.try_get_mut<FMeleeWeaponInstance>();
		if (Inst)
		{
			Inst->bBlockRequested = bRequested;
		}
	});
}
