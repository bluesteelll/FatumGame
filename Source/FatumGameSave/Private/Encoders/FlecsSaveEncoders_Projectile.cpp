// FlecsSaveEncoders_Projectile — implementations + static-init registry hooks.

#include "Encoders/FlecsSaveEncoders_Projectile.h"

#include "FlecsSaveComponentRegistry.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveRemap.h"
#include "FlecsSaveTypeIds.h"

#include "FlecsProjectileComponents.h"
#include "FlecsPenetrationComponents.h"
#include "FlecsGameTags.h"  // FTagProjectile, FTagCharacter

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "flecs.h"

namespace
{
	template <typename T>
	FORCEINLINE void SaveValue(FArchive& Ar, T Value)
	{
		T Tmp = Value;
		Ar << Tmp;
	}
}

// ═══════════════════════════════════════════════════════════════
// FProjectileInstance  (TypeId 0x0503, Version 1)
//
// OwnerEntityId is REMAP (may be player, NPC, or 0/no-owner). SpawnPosition is intrinsic
// (used by distance-falloff calc post-load — needs to survive verbatim).
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Projectile::Encode_ProjectileInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FProjectileInstance* C = E.try_get<FProjectileInstance>();
	checkf(C, TEXT("Encode_ProjectileInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<float>(Ar, C->LifetimeRemaining);
	SaveValue<int32>(Ar, C->BounceCount);
	SaveValue<int32>(Ar, C->GraceFramesRemaining);
	SaveValue<float>(Ar, C->FuseRemaining);

	const uint32 OwnerIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(C->OwnerEntityId));
	SaveValue<uint32>(Ar, OwnerIdx);

	SaveValue<double>(Ar, C->SpawnPosition.X);
	SaveValue<double>(Ar, C->SpawnPosition.Y);
	SaveValue<double>(Ar, C->SpawnPosition.Z);
}

bool FlecsSaveEncoders_Projectile::Decode_ProjectileInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_ProjectileInstance: unknown version %u"), Version);
		return false;
	}

	FProjectileInstance C;
	uint32 OwnerIdx = 0xFFFFFFFFu;
	Ar << C.LifetimeRemaining;
	Ar << C.BounceCount;
	Ar << C.GraceFramesRemaining;
	Ar << C.FuseRemaining;
	Ar << OwnerIdx;
	Ar << C.SpawnPosition.X;
	Ar << C.SpawnPosition.Y;
	Ar << C.SpawnPosition.Z;

	C.OwnerEntityId = static_cast<int64>(FlecsSaveRemap::ResolveSaveIndex(OwnerIdx));

	E.set<FProjectileInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FPenetrationInstance  (TypeId 0x0504, Version 1)
//
// LastPenetratedTargetId is REMAP (entity_t of the last target the bullet passed through;
// 0 sentinel = none). On load, if the target was dropped from the save set, this becomes
// 0 — which is the correct semantic for "no prior penetration target".
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Projectile::Encode_PenetrationInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FPenetrationInstance* C = E.try_get<FPenetrationInstance>();
	checkf(C, TEXT("Encode_PenetrationInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<float>(Ar, C->RemainingBudget);
	SaveValue<int32>(Ar, C->PenetrationCount);
	SaveValue<float>(Ar, C->CurrentDamageMultiplier);

	const uint32 LastTargetIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(C->LastPenetratedTargetId));
	SaveValue<uint32>(Ar, LastTargetIdx);
}

bool FlecsSaveEncoders_Projectile::Decode_PenetrationInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_PenetrationInstance: unknown version %u"), Version);
		return false;
	}

	FPenetrationInstance C;
	uint32 LastTargetIdx = 0xFFFFFFFFu;
	Ar << C.RemainingBudget;
	Ar << C.PenetrationCount;
	Ar << C.CurrentDamageMultiplier;
	Ar << LastTargetIdx;

	C.LastPenetratedTargetId = static_cast<uint64>(FlecsSaveRemap::ResolveSaveIndex(LastTargetIdx));

	E.set<FPenetrationInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FSurfaceIntegrity  (TypeId 0x0505, Version 1)
//
// 148-byte POD: uint16 Integrity[64] + float GridOriginU/V + InvCellSizeU/V +
// uint8 ActiveCols/Rows + Projection + pad. No pointers, no cross-entity refs.
// Serialized as a raw block with a defensive static_assert on the struct size so
// any future field addition surfaces immediately at compile time.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Projectile::Encode_SurfaceIntegrity(const flecs::entity& E, TArray<uint8>& Out)
{
	static_assert(sizeof(FSurfaceIntegrity) == 148,
		"FSurfaceIntegrity layout changed — bump the encoder version + adjust the on-disk layout");

	const FSurfaceIntegrity* C = E.try_get<FSurfaceIntegrity>();
	checkf(C, TEXT("Encode_SurfaceIntegrity: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	uint16 Version = 1;
	Ar << Version;

	// Raw 148-byte block (POD; no pointers; layout asserted above).
	Ar.Serialize(const_cast<FSurfaceIntegrity*>(C), sizeof(FSurfaceIntegrity));
}

bool FlecsSaveEncoders_Projectile::Decode_SurfaceIntegrity(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	static_assert(sizeof(FSurfaceIntegrity) == 148,
		"FSurfaceIntegrity layout changed — bump the encoder version + adjust the on-disk layout");

	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_SurfaceIntegrity: unknown version %u"), Version);
		return false;
	}

	if (Len < sizeof(uint16) + sizeof(FSurfaceIntegrity))
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("Decode_SurfaceIntegrity: payload too small (%u bytes; need %zu)"),
			Len, sizeof(uint16) + sizeof(FSurfaceIntegrity));
		return false;
	}

	FSurfaceIntegrity C;
	Ar.Serialize(&C, sizeof(FSurfaceIntegrity));

	E.set<FSurfaceIntegrity>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// AUTO-REGISTRATION
// ═══════════════════════════════════════════════════════════════

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_ProjectileInstance, FProjectileInstance, 1,
	FlecsSaveEncoders_Projectile::Encode_ProjectileInstance,
	FlecsSaveEncoders_Projectile::Decode_ProjectileInstance)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_PenetrationInstance, FPenetrationInstance, 1,
	FlecsSaveEncoders_Projectile::Encode_PenetrationInstance,
	FlecsSaveEncoders_Projectile::Decode_PenetrationInstance)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_SurfaceIntegrity, FSurfaceIntegrity, 1,
	FlecsSaveEncoders_Projectile::Encode_SurfaceIntegrity,
	FlecsSaveEncoders_Projectile::Decode_SurfaceIntegrity)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagProjectile, FTagProjectile)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagCharacter, FTagCharacter)
