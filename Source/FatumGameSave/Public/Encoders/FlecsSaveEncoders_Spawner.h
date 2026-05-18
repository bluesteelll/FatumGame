// FlecsSaveEncoders_Spawner — encoders for spawning-domain components (Phase 5).
//
// Components:
//   FSpawnerProvenance (TypeId 0x0900) — level-path + actor-name of the spawner that
//                                         produced this entity. Used by the save subsystem
//                                         on load to mark matching spawners with
//                                         bSavedEntityOverridesMe so they skip BeginPlay
//                                         spawn (dedup).
//
// Tags:
//   FTagPlayerCharacter (TypeId 0x1001)
//
// FTagSpawnedByLevelSpawner is transient and NOT registered for save round-trip.

#pragma once

#include "CoreMinimal.h"

namespace flecs { struct entity; }

namespace FlecsSaveEncoders_Spawner
{
	void Encode_SpawnerProvenance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_SpawnerProvenance(const flecs::entity& E, const uint8* Bytes, uint32 Len);
}
