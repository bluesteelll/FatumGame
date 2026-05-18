// FlecsSaveEncoders_Projectile — encoders for projectile + penetration components (Phase 5).
//
// Components:
//   FProjectileInstance  (TypeId 0x0503) — flight state + OwnerEntityId (REMAP) + SpawnPosition.
//   FPenetrationInstance (TypeId 0x0504) — penetration budget + LastPenetratedTargetId (REMAP).
//   FSurfaceIntegrity    (TypeId 0x0505) — 148-byte raw block (Integrity grid + projection).
//
// Tags:
//   FTagProjectile  (TypeId 0x1503)
//   FTagCharacter   (TypeId 0x1504)   — registered here as a convenience because the
//                                       Phase 5 walker exclusion list removed FTagDead
//                                       gating, and characters do need round-trip.

#pragma once

#include "CoreMinimal.h"

namespace flecs { struct entity; }

namespace FlecsSaveEncoders_Projectile
{
	void Encode_ProjectileInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_ProjectileInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_PenetrationInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_PenetrationInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_SurfaceIntegrity(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_SurfaceIntegrity(const flecs::entity& E, const uint8* Bytes, uint32 Len);
}
