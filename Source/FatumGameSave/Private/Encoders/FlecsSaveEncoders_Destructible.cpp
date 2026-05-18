// FlecsSaveEncoders_Destructible — implementations + static-init registry hooks.

#include "Encoders/FlecsSaveEncoders_Destructible.h"

#include "FlecsSaveComponentRegistry.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveTypeIds.h"

#include "FlecsDestructibleComponents.h"
#include "FlecsGameTags.h"  // FTagDestructible

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "Encoders/FlecsSaveEncoderHelpers.h"   // SaveValue template — moved out of anonymous ns to fix unity-build collision

#include "flecs.h"

using FlecsSaveEnc::SaveValue;

// ═══════════════════════════════════════════════════════════════
// FDebrisInstance  (TypeId 0x0502, Version 1)
//
// ALL FIELDS PERSISTED — debris pieces are valid entities mid-flight; saving them is
// in scope per user Q5 (save mid-combat). PoolSlotIndex is preserved so the post-load
// re-bind can wire fragments back into the debris pool by slot if the pool happens to
// have matching free slots; otherwise the fragment lives as a standalone body.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Destructible::Encode_DebrisInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FDebrisInstance* C = E.try_get<FDebrisInstance>();
	checkf(C, TEXT("Encode_DebrisInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<float>(Ar, C->LifetimeRemaining);
	SaveValue<uint8>(Ar, C->bAutoDestroy ? 1u : 0u);
	SaveValue<int32>(Ar, C->PoolSlotIndex);
	SaveValue<float>(Ar, C->FreeMassKg);
	SaveValue<double>(Ar, C->PendingImpulse.X);
	SaveValue<double>(Ar, C->PendingImpulse.Y);
	SaveValue<double>(Ar, C->PendingImpulse.Z);
	SaveValue<uint8>(Ar, C->bInAnchoredStructure ? 1u : 0u);
}

bool FlecsSaveEncoders_Destructible::Decode_DebrisInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_DebrisInstance: unknown version %u"), Version);
		return false;
	}

	FDebrisInstance C;
	uint8 AutoDestroyByte = 0;
	uint8 AnchoredByte = 0;
	Ar << C.LifetimeRemaining;
	Ar << AutoDestroyByte;
	Ar << C.PoolSlotIndex;
	Ar << C.FreeMassKg;
	Ar << C.PendingImpulse.X;
	Ar << C.PendingImpulse.Y;
	Ar << C.PendingImpulse.Z;
	Ar << AnchoredByte;

	C.bAutoDestroy         = (AutoDestroyByte != 0);
	C.bInAnchoredStructure = (AnchoredByte    != 0);

	E.set<FDebrisInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// AUTO-REGISTRATION
// ═══════════════════════════════════════════════════════════════

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_DebrisInstance, FDebrisInstance, 1,
	FlecsSaveEncoders_Destructible::Encode_DebrisInstance,
	FlecsSaveEncoders_Destructible::Decode_DebrisInstance)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagDestructible, FTagDestructible)
