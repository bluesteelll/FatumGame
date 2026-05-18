// FlecsSaveEncoders_Core — encoder/decoder declarations for Core-domain components.
//
// Phase 2 set (TypeIds 0x0100–0x0108):
//   FEntityDefinitionRef       — special: registered as header-only sentinel, no encoder.
//   FFocusCameraOverride       — per-instance camera viewpoint.
//   FInteractionInstance       — toggle state + use count.
//   FInteractionAngleOverride  — per-instance cone restriction.
//   FHealthInstance            — current HP + regen accumulator.
//   FMovementState             — posture / move mode / speed / lean.
//   FResourcePools             — variable-length pool array.
//   FVitalsInstance            — hunger / thirst / warmth percent.
//   FStealthInstance           — light / noise / detectability.
//
// All encoders write a uint16 Version as the first 2 bytes; decoders read it first
// and either succeed or log + return false.

#pragma once

#include "CoreMinimal.h"

// Forward declare to avoid pulling flecs.h into the public header.
namespace flecs { struct entity; }

namespace FlecsSaveEncoders_Core
{
	// Each pair below is wired into the registry via REGISTER_SAVE_COMPONENT in the .cpp.
	// Public visibility lets unit tests call the encoder/decoder directly.

	void Encode_FocusCameraOverride(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_FocusCameraOverride(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_InteractionInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_InteractionInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_InteractionAngleOverride(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_InteractionAngleOverride(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_HealthInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_HealthInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_MovementState(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_MovementState(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_ResourcePools(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_ResourcePools(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_VitalsInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_VitalsInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_StealthInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_StealthInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);
}
