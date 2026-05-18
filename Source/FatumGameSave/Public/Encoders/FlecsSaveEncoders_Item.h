// FlecsSaveEncoders_Item — encoder/decoder declarations for Item/Container/Magazine components.
//
// Phase 3 set (TypeIds 0x0200–0x0209):
//   FItemInstance              — stack count.
//   FItemUniqueData            — durability, enchantments, custom stats.
//   FItemTags                  — gameplay tag container.
//   FContainerInstance         — weight/count + OwnerEntityId (REMAP).
//   FContainerGridInstance     — occupancy bitmask.
//   FContainerSlotsInstance    — TMap<int32, int64> SlotToItemEntity (each value REMAP).
//   FWorldItemInstance         — despawn / pickup-grace + DroppedByEntityId (REMAP).
//   FContainedIn               — ContainerEntityId (REMAP) + grid/slot position.
//   FMagazineInstance          — variable-length LIFO ammo stack.
//   FAmmoTypeRef               — ammo type index on loose-ammo items.
//
// All encoders write a uint16 Version as the first 2 bytes; decoders read it first
// and either succeed or log + return false.
//
// Cross-entity refs (ContainerEntityId, OwnerEntityId, DroppedByEntityId,
// SlotToItemEntity values) use FlecsSaveRemap::EntityToSaveIndex on write +
// FlecsSaveRemap::ResolveSaveIndex on read. Unresolvable refs become 0 on load.

#pragma once

#include "CoreMinimal.h"

namespace flecs { struct entity; }

namespace FlecsSaveEncoders_Item
{
	void Encode_ItemInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_ItemInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_ItemUniqueData(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_ItemUniqueData(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_ItemTags(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_ItemTags(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_ContainerInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_ContainerInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_ContainerGridInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_ContainerGridInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_ContainerSlotsInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_ContainerSlotsInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_WorldItemInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_WorldItemInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_ContainedIn(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_ContainedIn(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_MagazineInstance(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_MagazineInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len);

	void Encode_AmmoTypeRef(const flecs::entity& E, TArray<uint8>& OutBytes);
	bool Decode_AmmoTypeRef(const flecs::entity& E, const uint8* Bytes, uint32 Len);
}
