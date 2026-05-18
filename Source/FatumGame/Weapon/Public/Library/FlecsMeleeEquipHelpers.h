// FlecsMeleeEquipHelpers — sim-thread helpers shared between the weapon-equip system
// and the save-system post-decode rebind pass.
//
// Per v3 §B: AllocateBladeBufferForMeleeEntity is extracted from
// WeaponEquipSystem.cpp:236-247 so the save system can re-allocate BladeBuffers for
// every entity carrying FMeleeWeaponInstance after a load (the encoder skipped the
// pointer; the decoder zeroed it — the BladeBuffer is a raw pointer to a triple
// buffer that has no on-disk representation).
//
// Caller MUST hold sim-thread access (BladeBuffer is sim-side state; raw new/delete).

#pragma once

#include "CoreMinimal.h"

// Forward declare to avoid pulling flecs.h into the public header.
namespace flecs { struct entity; }

namespace FlecsMeleeEquip
{
	/** Allocates BladeBuffer on the given melee weapon entity. Idempotent: safely frees
	 *  any prior buffer before allocating a fresh one. Fails fast (checkf) if the entity
	 *  lacks FMeleeWeaponInstance.
	 *
	 *  Sim-thread only. */
	FATUMGAME_API void AllocateBladeBufferForMeleeEntity(flecs::entity MeleeWeaponEntity);
}
