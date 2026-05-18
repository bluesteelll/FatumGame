// FlecsSaveTags — zero-size Flecs tags introduced by the save system (Phase 5).
//
// FTagSpawnedByLevelSpawner — TRANSIENT runtime marker on entities created by an
//   AFlecsEntitySpawner. Useful for diagnostics but NOT round-tripped through save:
//   the persistent identity lives in FSpawnerProvenance (FlecsSpawnerComponents.h).
//
// FTagPlayerCharacter — identifies the unique player-controlled character entity in
//   1P single-player. Set in AFlecsCharacter::InitECSRegistration. Saved + restored.
//   Used by the load path to look up the rebuilt player entity and re-bind the actor
//   to it via RegisterCharacterBridge.
//
// Both tags MUST be registered in
// FlecsArtillerySubsystem_Systems.cpp::RegisterFlecsComponents (otherwise
// World.component<T>() is missing → first add<T>() crashes with 0x80000003 per the
// MEMORY.md "Flecs Component Registration" rule).

#pragma once

#include "CoreMinimal.h"

/** Transient marker: this entity was created by an AFlecsEntitySpawner this session.
 *  Not strictly required for save round-trip (FSpawnerProvenance carries the persistent
 *  identity), but useful for level-design diagnostics. */
struct FTagSpawnedByLevelSpawner {};

/** Identifies the unique player-controlled character entity. Exactly one entity should
 *  carry this tag at any moment (1P single-player). Used by save/load to rebind the
 *  AFlecsCharacter actor to the restored entity after a wipe. */
struct FTagPlayerCharacter {};
