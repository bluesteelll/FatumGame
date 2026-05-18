// FlecsSaveEncoders_Weapon — encoder/decoder declarations for Weapon-domain components.
//
// Phase 3 set (TypeIds 0x0300–0x0302):
//   FWeaponInstance       — magazine/reload/bloom/cycle/quickload. Charge state ZEROED on
//                           load (v2 §5.5: bIsCharging, ChargeAccumulator, payloads, all
//                           input-flag bools reset to false). Player must re-press fire.
//   FEquippedBy           — CharacterEntityId (REMAP) + SlotId.
//   FWeaponSlotState      — active slot + equip phase + WeaponSlotContainerId (REMAP).
//
// All encoders write a uint16 Version as the first 2 bytes; decoders read it first
// and either succeed or log + return false.

#pragma once

#include "CoreMinimal.h"

namespace flecs { struct entity; }

namespace FlecsSaveEncoders_Weapon
{
	void Encode_WeaponInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_WeaponInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_EquippedBy(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_EquippedBy(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_WeaponSlotState(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_WeaponSlotState(const flecs::entity& E, const uint8* Bytes, uint32 Len);
}
