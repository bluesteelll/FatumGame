// FlecsMeleeEquipHelpers — implementation.

#include "Library/FlecsMeleeEquipHelpers.h"

#include "FlecsMeleeComponents.h"

#include "flecs.h"

void FlecsMeleeEquip::AllocateBladeBufferForMeleeEntity(flecs::entity MeleeWeaponEntity)
{
	FMeleeWeaponInstance* MWI = MeleeWeaponEntity.try_get_mut<FMeleeWeaponInstance>();
	checkf(MWI,
		TEXT("AllocateBladeBufferForMeleeEntity: entity %llu lacks FMeleeWeaponInstance"),
		static_cast<uint64>(MeleeWeaponEntity.id()));

	// Defensive: free any lingering buffer from a prior equip / re-allocation cycle.
	// Double-allocation would leak the previous pointer past the on_remove<FMeleeWeaponInstance>
	// hook (which only frees the *current* pointer).
	if (MWI->BladeBuffer)
	{
		delete MWI->BladeBuffer;
		MWI->BladeBuffer = nullptr;
	}

	// Seed with a zero sample so first reader gets sane defaults (FrameStamp=0 means
	// "no writer pass yet").
	FMeleeWeaponInstance::FBladeSocketData DefaultSample;
	MWI->BladeBuffer = new FBladeSocketTripleBuffer(DefaultSample);
}
