// FlecsSaveEncoders_Spawner — implementations + static-init registry hooks.

#include "Encoders/FlecsSaveEncoders_Spawner.h"

#include "FlecsSaveComponentRegistry.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveTags.h"   // FTagPlayerCharacter
#include "FlecsSaveTypeIds.h"

#include "FlecsSpawnerComponents.h"  // FSpawnerProvenance

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "flecs.h"

// ═══════════════════════════════════════════════════════════════
// FSpawnerProvenance  (TypeId 0x0900, Version 1)
//
// Two FNames serialized via FArchive operator<<. FArchive stringifies FNames against
// the editor's name table, so missing names on load become empty FNames silently (the
// dedup pass simply won't find a match).
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Spawner::Encode_SpawnerProvenance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FSpawnerProvenance* C = E.try_get<FSpawnerProvenance>();
	checkf(C, TEXT("Encode_SpawnerProvenance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	uint16 Version = 1;
	Ar << Version;

	FName LevelPathTmp = C->SpawnerLevelPath;
	FName ActorNameTmp = C->SpawnerActorName;
	Ar << LevelPathTmp;
	Ar << ActorNameTmp;
}

bool FlecsSaveEncoders_Spawner::Decode_SpawnerProvenance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_SpawnerProvenance: unknown version %u"), Version);
		return false;
	}

	FSpawnerProvenance C;
	Ar << C.SpawnerLevelPath;
	Ar << C.SpawnerActorName;

	E.set<FSpawnerProvenance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// AUTO-REGISTRATION
// ═══════════════════════════════════════════════════════════════

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_SpawnerProvenance, FSpawnerProvenance, 1,
	FlecsSaveEncoders_Spawner::Encode_SpawnerProvenance,
	FlecsSaveEncoders_Spawner::Decode_SpawnerProvenance)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagPlayerCharacter, FTagPlayerCharacter)
