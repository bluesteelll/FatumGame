// Spawner-domain Flecs components — Phase 5 save system.
//
// FSpawnerProvenance is set by AFlecsEntitySpawner::SpawnEntity at spawn time so that
// the save walker can later identify which entities originated from which level-placed
// spawner actors. On load, the save subsystem scans saved entities for these tuples and
// marks matching spawners with bSavedEntityOverridesMe so they skip their BeginPlay spawn.
//
// Per v2 §5.3: identity = (FName SpawnerLevelPath, FName SpawnerActorName). PIE prefix
// (`UEDPIE_N_`) is stripped via UWorld::RemovePIEPrefix at capture time so save-in-PIE
// matches load-in-PIE regardless of instance id.
//
// FSpawnerProvenance is registered as a Flecs component in
// FlecsArtillerySubsystem_Systems.cpp::RegisterFlecsComponents.

#pragma once

#include "CoreMinimal.h"

/**
 * Records the level path + actor name of the AFlecsEntitySpawner that produced this entity.
 * Saved + restored. Used by the load-time spawner-dedup pass to skip BeginPlay spawn on
 * spawners whose entities are already present in the snapshot.
 */
struct FSpawnerProvenance
{
	/** Level path with PIE prefix stripped (e.g. "/Game/Maps/StartMap.StartMap:PersistentLevel"). */
	FName SpawnerLevelPath;

	/** FName of the spawner AActor in that level (GetFName()). */
	FName SpawnerActorName;

	bool IsValid() const { return !SpawnerLevelPath.IsNone() && !SpawnerActorName.IsNone(); }
};
