// FlecsSaveEncoders_Core — implementations + static-init registry hooks.

#include "Encoders/FlecsSaveEncoders_Core.h"

#include "FlecsSaveComponentRegistry.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveTypeIds.h"

#include "FlecsGameTags.h"                  // FTagInteractable
#include "FlecsEntityComponents.h"          // FFocusCameraOverride, FEntityDefinitionRef
#include "FlecsInteractionComponents.h"     // FInteractionInstance, FInteractionAngleOverride
#include "FlecsHealthComponents.h"          // FHealthInstance
#include "FlecsMovementComponents.h"        // FMovementState
#include "FlecsResourceTypes.h"             // FResourcePools, MAX_RESOURCE_POOLS
#include "FlecsVitalsComponents.h"          // FVitalsInstance
#include "FlecsStealthComponents.h"         // FStealthInstance

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "Encoders/FlecsSaveEncoderHelpers.h"   // SaveValue template — moved out of anonymous ns to fix unity-build collision

#include "flecs.h"

// Pull SaveValue<T> into the file scope so the per-encoder bodies below stay terse —
// see FlecsSaveEncoderHelpers.h for why the template lives in a named namespace.
using FlecsSaveEnc::SaveValue;

// ═══════════════════════════════════════════════════════════════
// FFocusCameraOverride  (TypeId 0x0101, Version 1)
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Core::Encode_FocusCameraOverride(const flecs::entity& E, TArray<uint8>& Out)
{
	const FFocusCameraOverride* C = E.try_get<FFocusCameraOverride>();
	checkf(C, TEXT("Encode_FocusCameraOverride: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<double>(Ar, C->CameraPosition.X);
	SaveValue<double>(Ar, C->CameraPosition.Y);
	SaveValue<double>(Ar, C->CameraPosition.Z);
	SaveValue<double>(Ar, C->CameraRotation.Pitch);
	SaveValue<double>(Ar, C->CameraRotation.Yaw);
	SaveValue<double>(Ar, C->CameraRotation.Roll);
}

bool FlecsSaveEncoders_Core::Decode_FocusCameraOverride(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_FocusCameraOverride: unknown version %u"), Version);
		return false;
	}

	FFocusCameraOverride C;
	Ar << C.CameraPosition.X;
	Ar << C.CameraPosition.Y;
	Ar << C.CameraPosition.Z;
	Ar << C.CameraRotation.Pitch;
	Ar << C.CameraRotation.Yaw;
	Ar << C.CameraRotation.Roll;

	E.set<FFocusCameraOverride>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FInteractionInstance  (TypeId 0x0102, Version 1)
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Core::Encode_InteractionInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FInteractionInstance* C = E.try_get<FInteractionInstance>();
	checkf(C, TEXT("Encode_InteractionInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<uint8>(Ar, C->bToggleState ? 1u : 0u);
	SaveValue<int32>(Ar, C->UseCount);
}

bool FlecsSaveEncoders_Core::Decode_InteractionInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_InteractionInstance: unknown version %u"), Version);
		return false;
	}

	uint8 ToggleByte = 0; Ar << ToggleByte;
	int32 UseCount = 0;   Ar << UseCount;

	FInteractionInstance C;
	C.bToggleState = (ToggleByte != 0);
	C.UseCount = UseCount;
	E.set<FInteractionInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FInteractionAngleOverride  (TypeId 0x0103, Version 1)
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Core::Encode_InteractionAngleOverride(const flecs::entity& E, TArray<uint8>& Out)
{
	const FInteractionAngleOverride* C = E.try_get<FInteractionAngleOverride>();
	checkf(C, TEXT("Encode_InteractionAngleOverride: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<float>(Ar, C->AngleCosine);
	SaveValue<double>(Ar, C->Direction.X);
	SaveValue<double>(Ar, C->Direction.Y);
	SaveValue<double>(Ar, C->Direction.Z);
}

bool FlecsSaveEncoders_Core::Decode_InteractionAngleOverride(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_InteractionAngleOverride: unknown version %u"), Version);
		return false;
	}

	FInteractionAngleOverride C;
	Ar << C.AngleCosine;
	Ar << C.Direction.X;
	Ar << C.Direction.Y;
	Ar << C.Direction.Z;
	E.set<FInteractionAngleOverride>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FHealthInstance  (TypeId 0x0104, Version 1)
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Core::Encode_HealthInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FHealthInstance* C = E.try_get<FHealthInstance>();
	checkf(C, TEXT("Encode_HealthInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<float>(Ar, C->CurrentHP);
	SaveValue<float>(Ar, C->RegenAccumulator);
}

bool FlecsSaveEncoders_Core::Decode_HealthInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_HealthInstance: unknown version %u"), Version);
		return false;
	}

	FHealthInstance C;
	Ar << C.CurrentHP;
	Ar << C.RegenAccumulator;
	E.set<FHealthInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FMovementState  (TypeId 0x0105, Version 1)
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Core::Encode_MovementState(const flecs::entity& E, TArray<uint8>& Out)
{
	const FMovementState* C = E.try_get<FMovementState>();
	checkf(C, TEXT("Encode_MovementState: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<uint8>(Ar, C->Posture);
	SaveValue<uint8>(Ar, C->MoveMode);
	SaveValue<float>(Ar, C->Speed);
	SaveValue<float>(Ar, C->VerticalSpeed);
	SaveValue<uint8>(Ar, C->LeanDir);
}

bool FlecsSaveEncoders_Core::Decode_MovementState(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_MovementState: unknown version %u"), Version);
		return false;
	}

	FMovementState C;
	Ar << C.Posture;
	Ar << C.MoveMode;
	Ar << C.Speed;
	Ar << C.VerticalSpeed;
	Ar << C.LeanDir;
	E.set<FMovementState>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FResourcePools  (TypeId 0x0106, Version 1)
// Variable-length: writes uint8 PoolCount then PoolCount × FResourcePool blob.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Core::Encode_ResourcePools(const flecs::entity& E, TArray<uint8>& Out)
{
	const FResourcePools* C = E.try_get<FResourcePools>();
	checkf(C, TEXT("Encode_ResourcePools: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));
	checkf(C->PoolCount <= MAX_RESOURCE_POOLS,
		TEXT("Encode_ResourcePools: entity %llu has PoolCount=%u exceeding MAX_RESOURCE_POOLS=%d"),
		static_cast<uint64>(E.id()), C->PoolCount, MAX_RESOURCE_POOLS);

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<uint8>(Ar, C->PoolCount);

	for (int32 i = 0; i < C->PoolCount; ++i)
	{
		const FResourcePool& P = C->Pools[i];
		SaveValue<uint8>(Ar, static_cast<uint8>(P.TypeId));
		SaveValue<float>(Ar, P.MaxValue);
		SaveValue<float>(Ar, P.CurrentValue);
		SaveValue<float>(Ar, P.BaseRegenRate);
		SaveValue<float>(Ar, P.RegenDelay);
		SaveValue<float>(Ar, P.RegenDelayTimer);
		SaveValue<float>(Ar, P.RegenAccumulator);
		SaveValue<uint8>(Ar, P.bRegenWhileChanneling ? 1u : 0u);
	}
}

bool FlecsSaveEncoders_Core::Decode_ResourcePools(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_ResourcePools: unknown version %u"), Version);
		return false;
	}

	uint8 PoolCount = 0; Ar << PoolCount;
	if (PoolCount > MAX_RESOURCE_POOLS)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("Decode_ResourcePools: corrupt PoolCount=%u exceeds MAX_RESOURCE_POOLS=%d"),
			PoolCount, MAX_RESOURCE_POOLS);
		return false;
	}

	FResourcePools C;
	C.PoolCount = PoolCount;

	for (int32 i = 0; i < PoolCount; ++i)
	{
		uint8 TypeIdByte = 0; Ar << TypeIdByte;
		FResourcePool& P = C.Pools[i];
		P.TypeId = static_cast<EResourceTypeId>(TypeIdByte);
		Ar << P.MaxValue;
		Ar << P.CurrentValue;
		Ar << P.BaseRegenRate;
		Ar << P.RegenDelay;
		Ar << P.RegenDelayTimer;
		Ar << P.RegenAccumulator;
		uint8 RegenChannelByte = 0; Ar << RegenChannelByte;
		P.bRegenWhileChanneling = (RegenChannelByte != 0);
	}

	// Slots PoolCount…MAX_RESOURCE_POOLS-1 retain their default values from FResourcePools().

	E.set<FResourcePools>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FVitalsInstance  (TypeId 0x0107, Version 1)
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Core::Encode_VitalsInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FVitalsInstance* C = E.try_get<FVitalsInstance>();
	checkf(C, TEXT("Encode_VitalsInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<float>(Ar, C->HungerPercent);
	SaveValue<float>(Ar, C->ThirstPercent);
	SaveValue<float>(Ar, C->WarmthPercent);
	SaveValue<float>(Ar, C->HungerAccum);
	SaveValue<float>(Ar, C->ThirstAccum);
	SaveValue<float>(Ar, C->TargetWarmth);
	// bEquipmentDirty intentionally NOT serialized — always set to true on load so
	// EquipmentModifierSystem re-scans the (possibly remapped) inventory next tick.
}

bool FlecsSaveEncoders_Core::Decode_VitalsInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_VitalsInstance: unknown version %u"), Version);
		return false;
	}

	FVitalsInstance C;
	Ar << C.HungerPercent;
	Ar << C.ThirstPercent;
	Ar << C.WarmthPercent;
	Ar << C.HungerAccum;
	Ar << C.ThirstAccum;
	Ar << C.TargetWarmth;
	C.bEquipmentDirty = true; // force re-scan on first post-load tick
	E.set<FVitalsInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FStealthInstance  (TypeId 0x0108, Version 1)
// PendingNoise is transient (drained every tick) — NOT serialized.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Core::Encode_StealthInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FStealthInstance* C = E.try_get<FStealthInstance>();
	checkf(C, TEXT("Encode_StealthInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<float>(Ar, C->LightLevel);
	SaveValue<float>(Ar, C->NoiseLevel);
	SaveValue<float>(Ar, C->Detectability);
	SaveValue<float>(Ar, C->RawLightLevel);
	// PendingNoise[] not serialized — empty on next tick start anyway.
}

bool FlecsSaveEncoders_Core::Decode_StealthInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_StealthInstance: unknown version %u"), Version);
		return false;
	}

	FStealthInstance C;
	Ar << C.LightLevel;
	Ar << C.NoiseLevel;
	Ar << C.Detectability;
	Ar << C.RawLightLevel;
	// C.PendingNoise stays empty.
	E.set<FStealthInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// AUTO-REGISTRATION
// ═══════════════════════════════════════════════════════════════
//
// FEntityDefinitionRef is registered as a tag-shaped placeholder (no encoder/decoder)
// so the registry treats its name as "known but header-only". The walker checks the
// TypeId and skips emission of a component block — the per-entity-header path index
// is the canonical encoding.

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_FocusCameraOverride, FFocusCameraOverride, 1,
	FlecsSaveEncoders_Core::Encode_FocusCameraOverride,
	FlecsSaveEncoders_Core::Decode_FocusCameraOverride)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_InteractionInstance, FInteractionInstance, 1,
	FlecsSaveEncoders_Core::Encode_InteractionInstance,
	FlecsSaveEncoders_Core::Decode_InteractionInstance)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_InteractionAngleOverride, FInteractionAngleOverride, 1,
	FlecsSaveEncoders_Core::Encode_InteractionAngleOverride,
	FlecsSaveEncoders_Core::Decode_InteractionAngleOverride)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_HealthInstance, FHealthInstance, 1,
	FlecsSaveEncoders_Core::Encode_HealthInstance,
	FlecsSaveEncoders_Core::Decode_HealthInstance)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_MovementState, FMovementState, 1,
	FlecsSaveEncoders_Core::Encode_MovementState,
	FlecsSaveEncoders_Core::Decode_MovementState)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_ResourcePools, FResourcePools, 1,
	FlecsSaveEncoders_Core::Encode_ResourcePools,
	FlecsSaveEncoders_Core::Decode_ResourcePools)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_VitalsInstance, FVitalsInstance, 1,
	FlecsSaveEncoders_Core::Encode_VitalsInstance,
	FlecsSaveEncoders_Core::Decode_VitalsInstance)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_StealthInstance, FStealthInstance, 1,
	FlecsSaveEncoders_Core::Encode_StealthInstance,
	FlecsSaveEncoders_Core::Decode_StealthInstance)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagInteractable, FTagInteractable)
