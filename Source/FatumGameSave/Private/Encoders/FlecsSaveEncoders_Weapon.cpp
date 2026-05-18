// FlecsSaveEncoders_Weapon — implementations + static-init registry hooks.
//
// CRITICAL (v2 §5.5): Charge state is NEVER restored from save. Decoder explicitly
// zeros bIsCharging / ChargeAccumulator / Pending+Latched payloads / all input
// flags. Restoring half-charge with bFireRequested=false would trigger the release
// detector → auto-fire a half-charged shot on the first sim tick post-load.
//
// CRITICAL (v2 §5.5): All sim-thread input flags (bFireRequested, bReloadRequested,
// bFireTriggerPending, bReloadCancelRequested) are reset to false on load — they're
// game-thread→sim flags that need to start fresh after the player re-presses.

#include "Encoders/FlecsSaveEncoders_Weapon.h"

#include "FlecsSaveComponentRegistry.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveRemap.h"
#include "FlecsSaveTypeIds.h"

#include "FlecsWeaponComponents.h"  // FWeaponInstance, FEquippedBy, FWeaponSlotState,
                                    // FTagWeapon, EWeaponReloadPhase, EActiveLoadMethod,
                                    // EWeaponEquipPhase, FChargeShotPayload

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "Encoders/FlecsSaveEncoderHelpers.h"   // SaveValue template — moved out of anonymous ns to fix unity-build collision

#include "flecs.h"

using FlecsSaveEnc::SaveValue;

// ═══════════════════════════════════════════════════════════════
// FWeaponInstance  (TypeId 0x0300, Version 1)
//
// FIELDS PERSISTED (read on load):
//   - Magazine refs (InsertedMagazineId, SelectedMagazineId, ActiveDeviceEntityId — REMAP)
//   - Firing-cooldown state (FireCooldownRemaining, BurstShotsRemaining, BurstCooldownRemaining,
//                            bHasFiredSincePress)
//   - Reload state (ReloadPhase, ReloadPhaseTimer, bPrevMagWasEmpty, bChambered,
//                   ChamberedAmmoTypeIdx)
//   - Bloom state (CurrentBloom, TimeSinceLastShot, TriggerPullTimer, bTriggerPulling,
//                  ShotsFiredTotal)
//   - Cycling state (bNeedsCycle, bCycling, CycleTimeRemaining)
//   - Single-round reload (RoundsInsertedThisReload)
//   - Quick-load (ActiveLoadMethod, BatchSize, BatchInsertTime, DeviceAmmoTypeIdx,
//                 bUsedDeviceThisReload)
//   - LastShotAmmoTypeIdx
//
// FIELDS ZEROED on load (NOT read from blob — see v2 §5.5):
//   - bIsCharging, ChargeAccumulator, bWasFireRequestedLastTick, bPendingAutoRestart
//   - PendingPayload {}, LatchedPayload {}
//   - bFireRequested, bFireTriggerPending, bReloadRequested, bReloadCancelRequested
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Weapon::Encode_WeaponInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FWeaponInstance* C = E.try_get<FWeaponInstance>();
	checkf(C, TEXT("Encode_WeaponInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version

	// ─── Magazine refs (REMAP via reverse map) ──────────────────────────────
	const uint32 InsertedMagIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(C->InsertedMagazineId));
	const uint32 SelectedMagIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(C->SelectedMagazineId));
	const uint32 ActiveDeviceIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(C->ActiveDeviceEntityId));
	SaveValue<uint32>(Ar, InsertedMagIdx);
	SaveValue<uint32>(Ar, SelectedMagIdx);
	SaveValue<uint32>(Ar, ActiveDeviceIdx);

	// ─── Firing state ───────────────────────────────────────────────────────
	SaveValue<float>(Ar, C->FireCooldownRemaining);
	SaveValue<int32>(Ar, C->BurstShotsRemaining);
	SaveValue<float>(Ar, C->BurstCooldownRemaining);
	SaveValue<uint8>(Ar, C->bHasFiredSincePress ? 1u : 0u);

	// ─── Reload state ───────────────────────────────────────────────────────
	SaveValue<uint8>(Ar, static_cast<uint8>(C->ReloadPhase));
	SaveValue<float>(Ar, C->ReloadPhaseTimer);
	SaveValue<uint8>(Ar, C->bPrevMagWasEmpty ? 1u : 0u);
	SaveValue<uint8>(Ar, C->bChambered ? 1u : 0u);
	SaveValue<uint8>(Ar, C->ChamberedAmmoTypeIdx);

	// ─── Bloom state ────────────────────────────────────────────────────────
	SaveValue<float>(Ar, C->CurrentBloom);
	SaveValue<float>(Ar, C->TimeSinceLastShot);
	SaveValue<float>(Ar, C->TriggerPullTimer);
	SaveValue<uint8>(Ar, C->bTriggerPulling ? 1u : 0u);
	SaveValue<int32>(Ar, C->ShotsFiredTotal);

	// ─── Post-fire cycling state ────────────────────────────────────────────
	SaveValue<uint8>(Ar, C->bNeedsCycle ? 1u : 0u);
	SaveValue<uint8>(Ar, C->bCycling ? 1u : 0u);
	SaveValue<float>(Ar, C->CycleTimeRemaining);

	// ─── Single-round reload state ─────────────────────────────────────────
	SaveValue<int32>(Ar, C->RoundsInsertedThisReload);

	// ─── Quick-load device state ───────────────────────────────────────────
	SaveValue<uint8>(Ar, static_cast<uint8>(C->ActiveLoadMethod));
	SaveValue<int32>(Ar, C->BatchSize);
	SaveValue<float>(Ar, C->BatchInsertTime);
	SaveValue<uint8>(Ar, C->DeviceAmmoTypeIdx);
	SaveValue<uint8>(Ar, C->bUsedDeviceThisReload ? 1u : 0u);

	// ─── LastShotAmmoTypeIdx (the only meaningful charge-adjacent persistent value) ──
	SaveValue<int8>(Ar, C->LastShotAmmoTypeIdx);

	// CHARGE STATE, INPUT FLAGS: NOT serialized. Decoder zeroes them per v2 §5.5.
}

bool FlecsSaveEncoders_Weapon::Decode_WeaponInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_WeaponInstance: unknown version %u"), Version);
		return false;
	}

	FWeaponInstance C;

	// ─── Magazine refs (REMAP) ──────────────────────────────────────────────
	uint32 InsertedMagIdx = 0xFFFFFFFFu;
	uint32 SelectedMagIdx = 0xFFFFFFFFu;
	uint32 ActiveDeviceIdx = 0xFFFFFFFFu;
	Ar << InsertedMagIdx;
	Ar << SelectedMagIdx;
	Ar << ActiveDeviceIdx;
	C.InsertedMagazineId    = static_cast<int64>(FlecsSaveRemap::ResolveSaveIndex(InsertedMagIdx));
	C.SelectedMagazineId    = static_cast<int64>(FlecsSaveRemap::ResolveSaveIndex(SelectedMagIdx));
	C.ActiveDeviceEntityId  = static_cast<uint64>(FlecsSaveRemap::ResolveSaveIndex(ActiveDeviceIdx));

	// ─── Firing state ───────────────────────────────────────────────────────
	uint8 bHasFiredSincePressByte = 0;
	Ar << C.FireCooldownRemaining;
	Ar << C.BurstShotsRemaining;
	Ar << C.BurstCooldownRemaining;
	Ar << bHasFiredSincePressByte;
	C.bHasFiredSincePress = (bHasFiredSincePressByte != 0);

	// ─── Reload state ───────────────────────────────────────────────────────
	uint8 ReloadPhaseByte = 0;
	uint8 bPrevMagWasEmptyByte = 0;
	uint8 bChamberedByte = 0;
	Ar << ReloadPhaseByte;
	Ar << C.ReloadPhaseTimer;
	Ar << bPrevMagWasEmptyByte;
	Ar << bChamberedByte;
	Ar << C.ChamberedAmmoTypeIdx;
	C.ReloadPhase     = static_cast<EWeaponReloadPhase>(ReloadPhaseByte);
	C.bPrevMagWasEmpty = (bPrevMagWasEmptyByte != 0);
	C.bChambered      = (bChamberedByte != 0);

	// ─── Bloom state ────────────────────────────────────────────────────────
	uint8 bTriggerPullingByte = 0;
	Ar << C.CurrentBloom;
	Ar << C.TimeSinceLastShot;
	Ar << C.TriggerPullTimer;
	Ar << bTriggerPullingByte;
	Ar << C.ShotsFiredTotal;
	C.bTriggerPulling = (bTriggerPullingByte != 0);

	// ─── Post-fire cycling state ────────────────────────────────────────────
	uint8 bNeedsCycleByte = 0;
	uint8 bCyclingByte = 0;
	Ar << bNeedsCycleByte;
	Ar << bCyclingByte;
	Ar << C.CycleTimeRemaining;
	C.bNeedsCycle = (bNeedsCycleByte != 0);
	C.bCycling    = (bCyclingByte != 0);

	// ─── Single-round reload state ─────────────────────────────────────────
	Ar << C.RoundsInsertedThisReload;

	// ─── Quick-load device state ───────────────────────────────────────────
	uint8 ActiveLoadMethodByte = 0;
	uint8 bUsedDeviceThisReloadByte = 0;
	Ar << ActiveLoadMethodByte;
	Ar << C.BatchSize;
	Ar << C.BatchInsertTime;
	Ar << C.DeviceAmmoTypeIdx;
	Ar << bUsedDeviceThisReloadByte;
	C.ActiveLoadMethod        = static_cast<EActiveLoadMethod>(ActiveLoadMethodByte);
	C.bUsedDeviceThisReload   = (bUsedDeviceThisReloadByte != 0);

	// ─── LastShotAmmoTypeIdx ────────────────────────────────────────────────
	Ar << C.LastShotAmmoTypeIdx;

	// ─── CHARGE STATE — explicitly ZEROED on load (v2 §5.5) ────────────────
	// Restoring half-charge with bFireRequested=false would auto-fire the half-charged
	// shot on the first sim tick post-load. Player must re-press fire to resume.
	C.bIsCharging                = false;
	C.ChargeAccumulator          = 0.f;
	C.bWasFireRequestedLastTick  = false;
	C.bPendingAutoRestart        = false;
	C.PendingPayload             = FChargeShotPayload{};
	C.LatchedPayload             = FChargeShotPayload{};

	// ─── INPUT FLAGS — explicitly ZEROED on load (v2 §5.5) ──────────────────
	// Game-thread→sim input bools restart in their natural false state.
	C.bFireRequested          = false;
	C.bFireTriggerPending     = false;
	C.bReloadRequested        = false;
	C.bReloadCancelRequested  = false;

	E.set<FWeaponInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FEquippedBy  (TypeId 0x0301, Version 1)
// CharacterEntityId is REMAP. SlotId is intrinsic.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Weapon::Encode_EquippedBy(const flecs::entity& E, TArray<uint8>& Out)
{
	const FEquippedBy* C = E.try_get<FEquippedBy>();
	checkf(C, TEXT("Encode_EquippedBy: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version

	const uint32 CharIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(C->CharacterEntityId));
	SaveValue<uint32>(Ar, CharIdx);
	SaveValue<int32>(Ar, C->SlotId);
}

bool FlecsSaveEncoders_Weapon::Decode_EquippedBy(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_EquippedBy: unknown version %u"), Version);
		return false;
	}

	uint32 CharIdx = 0xFFFFFFFFu;
	int32 SlotId = 0;
	Ar << CharIdx;
	Ar << SlotId;

	FEquippedBy C;
	C.CharacterEntityId = static_cast<int64>(FlecsSaveRemap::ResolveSaveIndex(CharIdx));
	C.SlotId            = SlotId;

	E.set<FEquippedBy>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FWeaponSlotState  (TypeId 0x0302, Version 1)
// WeaponSlotContainerId is REMAP. Other fields intrinsic.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Weapon::Encode_WeaponSlotState(const flecs::entity& E, TArray<uint8>& Out)
{
	const FWeaponSlotState* C = E.try_get<FWeaponSlotState>();
	checkf(C, TEXT("Encode_WeaponSlotState: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<int32>(Ar, C->ActiveSlotIndex);
	SaveValue<int32>(Ar, C->PendingSlotIndex);
	SaveValue<uint8>(Ar, static_cast<uint8>(C->EquipPhase));
	SaveValue<float>(Ar, C->EquipTimer);

	const uint32 ContainerIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(C->WeaponSlotContainerId));
	SaveValue<uint32>(Ar, ContainerIdx);
}

bool FlecsSaveEncoders_Weapon::Decode_WeaponSlotState(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_WeaponSlotState: unknown version %u"), Version);
		return false;
	}

	FWeaponSlotState C;
	uint8 EquipPhaseByte = 0;
	uint32 ContainerIdx = 0xFFFFFFFFu;
	Ar << C.ActiveSlotIndex;
	Ar << C.PendingSlotIndex;
	Ar << EquipPhaseByte;
	Ar << C.EquipTimer;
	Ar << ContainerIdx;
	C.EquipPhase             = static_cast<EWeaponEquipPhase>(EquipPhaseByte);
	C.WeaponSlotContainerId  = static_cast<int64>(FlecsSaveRemap::ResolveSaveIndex(ContainerIdx));

	E.set<FWeaponSlotState>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// AUTO-REGISTRATION
// ═══════════════════════════════════════════════════════════════

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_WeaponInstance, FWeaponInstance, 1,
	FlecsSaveEncoders_Weapon::Encode_WeaponInstance,
	FlecsSaveEncoders_Weapon::Decode_WeaponInstance)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_EquippedBy, FEquippedBy, 1,
	FlecsSaveEncoders_Weapon::Encode_EquippedBy,
	FlecsSaveEncoders_Weapon::Decode_EquippedBy)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_WeaponSlotState, FWeaponSlotState, 1,
	FlecsSaveEncoders_Weapon::Encode_WeaponSlotState,
	FlecsSaveEncoders_Weapon::Decode_WeaponSlotState)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagWeapon, FTagWeapon)
