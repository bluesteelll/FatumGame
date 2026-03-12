// Types for weapon recoil feedback (sim thread → game thread).

#pragma once

#include "CoreMinimal.h"

/**
 * Shot-fired notification from sim thread to game thread.
 * One entry per trigger pull (not per pellet for shotguns).
 * Game thread drains this queue to apply kick, shake, and pattern recoil.
 */
struct FShotFiredEvent
{
	/** Weapon entity that fired. */
	int64 WeaponEntityId = 0;

	/** Running shot counter for this weapon (for pattern recoil indexing). */
	int32 ShotIndex = 0;
};
