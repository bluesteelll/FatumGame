// FlecsSaveEncoders_Destructible — encoders for destructible-domain components (Phase 5).
//
// Components:
//   FDebrisInstance (TypeId 0x0502) — lifetime, auto-destroy, pool slot, free mass,
//                                      pending impulse, anchored-structure flag.
//
// Tags:
//   FTagDestructible (TypeId 0x1502)
//
// FDestructibleStatic is NOT encoded — it carries a UFlecsDestructibleProfile* which
// comes from the prefab via FEntityDefinitionRef inheritance. It's reapplied at
// entity-create time by the IsA chain.

#pragma once

#include "CoreMinimal.h"

namespace flecs { struct entity; }

namespace FlecsSaveEncoders_Destructible
{
	void Encode_DebrisInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_DebrisInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);
}
