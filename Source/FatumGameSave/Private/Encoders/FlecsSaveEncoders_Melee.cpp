// FlecsSaveEncoders_Melee — implementations + static-init registry hooks.
//
// CRITICAL (v2 §5.5 + §5.6):
//   - FMeleeWeaponInstance decoder zeroes ALL charge/swing/phase/block state. Encoder
//     writes a stub version-only blob: nothing about the in-progress swing is meaningful
//     post-load because the BladeBuffer is freshly allocated empty and Phase=Charging
//     would re-cancel on the first sim tick anyway. Player must re-press to attack.
//   - BladeBuffer pointer is NEVER serialized. Decoder sets it to nullptr. A post-decode
//     rebind helper (added in Phase 5) iterates melee entities and calls
//     WeaponEquipSystem::AllocateBladeBufferForMeleeEntity. TODO marked in decoder.
//
// CRITICAL: FMeleeAttackDirectionBuffer is also transient — Reset() on load wipes it
// so any subsequent Charging→Resolve() classifies only post-load mouse motion.
//
// CRITICAL: FBladeSocketSync FrameStamp is reset to 0 so the sweep system's first-tick
// stale-frame guard triggers and seeds the prev-position fields properly.

#include "Encoders/FlecsSaveEncoders_Melee.h"

#include "FlecsSaveComponentRegistry.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveTypeIds.h"

#include "FlecsMeleeComponents.h"  // FMeleeWeaponInstance, FMeleeAttackDirectionBuffer,
                                   // FBladeSocketSync, FTagMeleeWeapon, EMeleeAttackPhase,
                                   // EMeleeSwingDirection, FMeleeChargePayload, FSwingPenState

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "Encoders/FlecsSaveEncoderHelpers.h"   // SaveValue template — moved out of anonymous ns to fix unity-build collision

#include "flecs.h"

using FlecsSaveEnc::SaveValue;

// ═══════════════════════════════════════════════════════════════
// FMeleeWeaponInstance  (TypeId 0x0400, Version 1)
//
// ENCODER: emits version + (effectively empty body). Nothing in the swing state
// graph survives load — see v2 §5.5/§5.6 rationale.
//
// DECODER: calls FMeleeWeaponInstance::ResetAllChargeAndSwingState() which zeroes
// every charge/swing/phase/block field but does NOT touch BladeBuffer. We then
// explicitly null BladeBuffer (v2 §5.6) and rely on the Phase 5 post-decode rebind
// to allocate fresh triple-buffer instances.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Melee::Encode_MeleeWeaponInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FMeleeWeaponInstance* C = E.try_get<FMeleeWeaponInstance>();
	checkf(C, TEXT("Encode_MeleeWeaponInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	// No persistent fields: charge state, payloads, phase, hit-dedup, sweep state,
	// block state — all transient. BladeBuffer is NEVER persisted (raw pointer per
	// v2 §5.6). Encoder is intentionally a stub; the blob carries the existence
	// signal alone so the decoder runs and zeroes the component on load.
}

bool FlecsSaveEncoders_Melee::Decode_MeleeWeaponInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_MeleeWeaponInstance: unknown version %u"), Version);
		return false;
	}

	// Default-construct: every field is already at its zero/idle default per the
	// header's in-class initializers. We then explicitly mirror FMeleeWeaponInstance::
	// ResetAllChargeAndSwingState() to satisfy v2 §5.5 (note: the project's reset
	// helper is not exported across module boundaries — inlining here avoids a
	// FatumGame DLL ABI change just for the save module).
	FMeleeWeaponInstance C;

	// Charge payloads cleared.
	C.PendingPayload = FMeleeChargePayload{};
	C.LatchedPayload = FMeleeChargePayload{};

	// Input flags cleared.
	C.bAttackRequested            = false;
	C.bBlockRequested             = false;
	C.bWasAttackRequestedLastTick = false;
	C.bWasBlockRequestedLastTick  = false;

	// Charge state cleared.
	C.bIsCharging         = false;
	C.ChargeAccumulator   = 0.f;
	C.bPendingAutoRestart = false;

	// Phase state cleared.
	C.Phase             = EMeleeAttackPhase::Idle;
	C.PhaseTimer        = 0.f;
	C.WindupDuration    = 0.f;
	C.ReleaseDuration   = 0.f;
	C.RecoveryDuration  = 0.f;
	C.ResolvedDirection = EMeleeSwingDirection::Horizontal;
	C.ShapedT           = 0.f;

	// Per-swing penetration state cleared.
	C.PenState = FSwingPenState{};

	// Hit dedup cleared.
	for (int32 i = 0; i < FMeleeWeaponInstance::MaxHitsPerSwing; ++i)
	{
		C.HitTargetIds[i] = 0;
	}
	C.HitCount = 0;

	// Sweep state cleared.
	C.PrevBladeStartWS    = FVector::ZeroVector;
	C.PrevBladeTipWS      = FVector::ZeroVector;
	C.LastSweepFrameStamp = 0;
	C.LastTipSpeed        = 0.f;
	C.bSwingRebounded     = false;
	C.bSwingBladeStuck    = false;

	// Block state cleared.
	C.bIsBlocking            = false;
	C.BlockStartTimestampSim = 0.f;

	// v2 §5.6: BladeBuffer is excluded from serialization entirely. Always null after
	// decode regardless of whether the entity had a buffer pre-save. A post-decode
	// pass in Phase 5 will call WeaponEquipSystem::AllocateBladeBufferForMeleeEntity(E)
	// for every entity carrying FMeleeWeaponInstance to allocate fresh buffers.
	// TODO(Phase 5): wire AllocateBladeBuffersForAllMeleeEntities(World) into the
	// snapshot-reader's post-decode pass (see v2 §5.6 helper).
	C.BladeBuffer = nullptr;

	E.set<FMeleeWeaponInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FMeleeAttackDirectionBuffer  (TypeId 0x0401, Version 1)
// Transient — AccumulatedDelta is only meaningful DURING Charging. Reset on load.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Melee::Encode_MeleeAttackDirectionBuffer(const flecs::entity& E, TArray<uint8>& Out)
{
	const FMeleeAttackDirectionBuffer* C = E.try_get<FMeleeAttackDirectionBuffer>();
	checkf(C, TEXT("Encode_MeleeAttackDirectionBuffer: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	// AccumulatedDelta NOT serialized (transient).
}

bool FlecsSaveEncoders_Melee::Decode_MeleeAttackDirectionBuffer(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("Decode_MeleeAttackDirectionBuffer: unknown version %u"), Version);
		return false;
	}

	FMeleeAttackDirectionBuffer C;
	C.Reset(); // AccumulatedDelta = (0,0) — redundant after default construct, kept explicit.

	E.set<FMeleeAttackDirectionBuffer>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FBladeSocketSync  (TypeId 0x0402, Version 1)
// FrameStamp reset to 0 so MeleeSweepSystem treats first post-load tick as fresh
// (its stale-frame guard checks LastSweepFrameStamp == 0). Blade positions are
// re-seeded on the next game-thread WriteMeleeWeaponBladeSocket call anyway.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Melee::Encode_BladeSocketSync(const flecs::entity& E, TArray<uint8>& Out)
{
	const FBladeSocketSync* C = E.try_get<FBladeSocketSync>();
	checkf(C, TEXT("Encode_BladeSocketSync: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	// Positions/FrameStamp NOT serialized — recomputed on first post-load tick.
}

bool FlecsSaveEncoders_Melee::Decode_BladeSocketSync(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_BladeSocketSync: unknown version %u"), Version);
		return false;
	}

	FBladeSocketSync C;
	C.BladeStartWS = FVector::ZeroVector;
	C.BladeTipWS   = FVector::ZeroVector;
	C.FrameStamp   = 0;

	E.set<FBladeSocketSync>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// AUTO-REGISTRATION
// ═══════════════════════════════════════════════════════════════

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_MeleeWeaponInstance, FMeleeWeaponInstance, 1,
	FlecsSaveEncoders_Melee::Encode_MeleeWeaponInstance,
	FlecsSaveEncoders_Melee::Decode_MeleeWeaponInstance)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_MeleeAttackDirectionBuffer, FMeleeAttackDirectionBuffer, 1,
	FlecsSaveEncoders_Melee::Encode_MeleeAttackDirectionBuffer,
	FlecsSaveEncoders_Melee::Decode_MeleeAttackDirectionBuffer)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_BladeSocketSync, FBladeSocketSync, 1,
	FlecsSaveEncoders_Melee::Encode_BladeSocketSync,
	FlecsSaveEncoders_Melee::Decode_BladeSocketSync)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagMeleeWeapon, FTagMeleeWeapon)
