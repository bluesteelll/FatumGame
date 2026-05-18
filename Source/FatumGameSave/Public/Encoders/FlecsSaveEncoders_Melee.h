// FlecsSaveEncoders_Melee — encoder/decoder declarations for Melee-domain components.
//
// Phase 3 set (TypeIds 0x0400–0x0402):
//   FMeleeWeaponInstance         — phase/charge/swing/sweep/block. ENTIRE charge+swing
//                                  state ZEROED on load via ResetAllChargeAndSwingState()
//                                  (v2 §5.5). BladeBuffer always nullptr on load
//                                  (v2 §5.6 — Phase 5 will add post-decode rebind helper).
//   FMeleeAttackDirectionBuffer  — integrated mouse delta. Reset on load (transient).
//   FBladeSocketSync             — blade socket snapshot. FrameStamp reset to 0 so the
//                                  sim's first-tick stale-frame guard triggers.
//
// All encoders write a uint16 Version as the first 2 bytes; decoders read it first
// and either succeed or log + return false.

#pragma once

#include "CoreMinimal.h"

namespace flecs { struct entity; }

namespace FlecsSaveEncoders_Melee
{
	void Encode_MeleeWeaponInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_MeleeWeaponInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_MeleeAttackDirectionBuffer(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_MeleeAttackDirectionBuffer(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_BladeSocketSync(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_BladeSocketSync(const flecs::entity& E, const uint8* Bytes, uint32 Len);
}
