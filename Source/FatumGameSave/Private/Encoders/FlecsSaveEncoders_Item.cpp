// FlecsSaveEncoders_Item — implementations + static-init registry hooks.
//
// Cross-entity refs go through FlecsSaveRemap:
//   - Encoder: FlecsSaveRemap::EntityToSaveIndex(LiveEntityId) → uint32 SaveIndex on disk.
//   - Decoder: FlecsSaveRemap::ResolveSaveIndex(SaveIndex) → new flecs::entity_t (or 0 sentinel
//     when the referent was skipped / not in the save set).
// The reader sets GRemapTable before Pass 1; the writer sets GReverseMap before encoding.

#include "Encoders/FlecsSaveEncoders_Item.h"

#include "FlecsSaveComponentRegistry.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveRemap.h"
#include "FlecsSaveTypeIds.h"

#include "FlecsGameTags.h"                  // FTagItem, FTagContainer, FTagPickupable
#include "FlecsItemComponents.h"            // FItemInstance, FItemUniqueData, FItemTags,
                                            // FContainerInstance/Grid/Slots, FContainedIn,
                                            // FWorldItemInstance, FMagazineInstance,
                                            // FAmmoTypeRef, FTagMagazine, FTagQuickLoadDevice

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "GameplayTagContainer.h"

#include "Encoders/FlecsSaveEncoderHelpers.h"   // SaveValue template — moved out of anonymous ns to fix unity-build collision

#include "flecs.h"

using FlecsSaveEnc::SaveValue;

// ═══════════════════════════════════════════════════════════════
// FItemInstance  (TypeId 0x0200, Version 1)
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Item::Encode_ItemInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FItemInstance* C = E.try_get<FItemInstance>();
	checkf(C, TEXT("Encode_ItemInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, /*bIsPersistent=*/ true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<int32>(Ar, C->Count);
}

bool FlecsSaveEncoders_Item::Decode_ItemInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), /*bIsPersistent=*/ true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_ItemInstance: unknown version %u"), Version);
		return false;
	}

	FItemInstance C;
	Ar << C.Count;
	E.set<FItemInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FItemUniqueData  (TypeId 0x0201, Version 1)
// Variable-length: EnchantmentIds (TArray<int32>) + CustomStats (TMap<FName, float>).
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Item::Encode_ItemUniqueData(const flecs::entity& E, TArray<uint8>& Out)
{
	const FItemUniqueData* C = E.try_get<FItemUniqueData>();
	checkf(C, TEXT("Encode_ItemUniqueData: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<float>(Ar, C->Durability);
	SaveValue<float>(Ar, C->MaxDurability);

	// EnchantmentIds.
	const int32 EnchantmentCount = C->EnchantmentIds.Num();
	SaveValue<int32>(Ar, EnchantmentCount);
	for (int32 i = 0; i < EnchantmentCount; ++i)
	{
		SaveValue<int32>(Ar, C->EnchantmentIds[i]);
	}

	// CustomStats: count then key/value pairs.
	const int32 StatCount = C->CustomStats.Num();
	SaveValue<int32>(Ar, StatCount);
	for (const auto& Pair : C->CustomStats)
	{
		FName KeyTmp = Pair.Key;
		Ar << KeyTmp;
		SaveValue<float>(Ar, Pair.Value);
	}
}

bool FlecsSaveEncoders_Item::Decode_ItemUniqueData(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_ItemUniqueData: unknown version %u"), Version);
		return false;
	}

	FItemUniqueData C;
	Ar << C.Durability;
	Ar << C.MaxDurability;

	int32 EnchantmentCount = 0;
	Ar << EnchantmentCount;
	if (EnchantmentCount < 0 || EnchantmentCount > 1024)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("Decode_ItemUniqueData: corrupt EnchantmentCount=%d"), EnchantmentCount);
		return false;
	}
	C.EnchantmentIds.Reserve(EnchantmentCount);
	for (int32 i = 0; i < EnchantmentCount; ++i)
	{
		int32 Id = 0; Ar << Id;
		C.EnchantmentIds.Add(Id);
	}

	int32 StatCount = 0;
	Ar << StatCount;
	if (StatCount < 0 || StatCount > 1024)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("Decode_ItemUniqueData: corrupt StatCount=%d"), StatCount);
		return false;
	}
	C.CustomStats.Reserve(StatCount);
	for (int32 i = 0; i < StatCount; ++i)
	{
		FName Key;
		float Value = 0.f;
		Ar << Key;
		Ar << Value;
		C.CustomStats.Add(Key, Value);
	}

	E.set<FItemUniqueData>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FItemTags  (TypeId 0x0202, Version 1)
// FGameplayTagContainer serialized as count + per-tag FName.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Item::Encode_ItemTags(const flecs::entity& E, TArray<uint8>& Out)
{
	const FItemTags* C = E.try_get<FItemTags>();
	checkf(C, TEXT("Encode_ItemTags: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version

	const int32 TagCount = C->Tags.Num();
	SaveValue<int32>(Ar, TagCount);
	for (const FGameplayTag& Tag : C->Tags)
	{
		FName TagName = Tag.GetTagName();
		Ar << TagName;
	}
}

bool FlecsSaveEncoders_Item::Decode_ItemTags(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_ItemTags: unknown version %u"), Version);
		return false;
	}

	int32 TagCount = 0;
	Ar << TagCount;
	if (TagCount < 0 || TagCount > 256)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_ItemTags: corrupt TagCount=%d"), TagCount);
		return false;
	}

	FItemTags C;
	for (int32 i = 0; i < TagCount; ++i)
	{
		FName TagName;
		Ar << TagName;
		// RequestGameplayTag is the canonical "look up by name". Use ErrorIfNotFound=false:
		// if the tag was renamed/removed between saves the entry is silently dropped.
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName, /*ErrorIfNotFound=*/false);
		if (Tag.IsValid())
		{
			C.Tags.AddTag(Tag);
		}
		else
		{
			UE_LOG(LogFlecsSave, Warning,
				TEXT("Decode_ItemTags: gameplay tag '%s' no longer exists — dropping"),
				*TagName.ToString());
		}
	}

	E.set<FItemTags>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FContainerInstance  (TypeId 0x0203, Version 1)
// OwnerEntityId is REMAP via FlecsSaveRemap.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Item::Encode_ContainerInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FContainerInstance* C = E.try_get<FContainerInstance>();
	checkf(C, TEXT("Encode_ContainerInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<float>(Ar, C->CurrentWeight);
	SaveValue<int32>(Ar, C->CurrentCount);

	const uint32 OwnerIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(C->OwnerEntityId));
	SaveValue<uint32>(Ar, OwnerIdx);
}

bool FlecsSaveEncoders_Item::Decode_ContainerInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_ContainerInstance: unknown version %u"), Version);
		return false;
	}

	FContainerInstance C;
	Ar << C.CurrentWeight;
	Ar << C.CurrentCount;

	uint32 OwnerIdx = 0xFFFFFFFFu;
	Ar << OwnerIdx;
	C.OwnerEntityId = static_cast<int64>(FlecsSaveRemap::ResolveSaveIndex(OwnerIdx));

	E.set<FContainerInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FContainerGridInstance  (TypeId 0x0204, Version 1)
// OccupancyMask is a TArray<uint8> bitmask — length-prefixed raw bytes.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Item::Encode_ContainerGridInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FContainerGridInstance* C = E.try_get<FContainerGridInstance>();
	checkf(C, TEXT("Encode_ContainerGridInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	const int32 MaskBytes = C->OccupancyMask.Num();
	SaveValue<int32>(Ar, MaskBytes);
	if (MaskBytes > 0)
	{
		Ar.Serialize(const_cast<uint8*>(C->OccupancyMask.GetData()), MaskBytes);
	}
}

bool FlecsSaveEncoders_Item::Decode_ContainerGridInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_ContainerGridInstance: unknown version %u"), Version);
		return false;
	}

	int32 MaskBytes = 0;
	Ar << MaskBytes;
	if (MaskBytes < 0 || MaskBytes > 65536)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("Decode_ContainerGridInstance: corrupt MaskBytes=%d"), MaskBytes);
		return false;
	}

	FContainerGridInstance C;
	C.OccupancyMask.SetNumZeroed(MaskBytes);
	if (MaskBytes > 0)
	{
		Ar.Serialize(C.OccupancyMask.GetData(), MaskBytes);
	}

	E.set<FContainerGridInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FContainerSlotsInstance  (TypeId 0x0205, Version 1)
// SlotToItemEntity TMap<int32, int64> — each value is a Flecs entity_t (REMAP).
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Item::Encode_ContainerSlotsInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FContainerSlotsInstance* C = E.try_get<FContainerSlotsInstance>();
	checkf(C, TEXT("Encode_ContainerSlotsInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version

	const int32 SlotCount = C->SlotToItemEntity.Num();
	SaveValue<int32>(Ar, SlotCount);
	for (const auto& Pair : C->SlotToItemEntity)
	{
		SaveValue<int32>(Ar, Pair.Key);
		const uint32 ItemIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(Pair.Value));
		SaveValue<uint32>(Ar, ItemIdx);
	}
}

bool FlecsSaveEncoders_Item::Decode_ContainerSlotsInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_ContainerSlotsInstance: unknown version %u"), Version);
		return false;
	}

	int32 SlotCount = 0;
	Ar << SlotCount;
	if (SlotCount < 0 || SlotCount > 1024)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("Decode_ContainerSlotsInstance: corrupt SlotCount=%d"), SlotCount);
		return false;
	}

	FContainerSlotsInstance C;
	C.SlotToItemEntity.Reserve(SlotCount);
	for (int32 i = 0; i < SlotCount; ++i)
	{
		int32 SlotId = 0;
		uint32 ItemIdx = 0xFFFFFFFFu;
		Ar << SlotId;
		Ar << ItemIdx;
		const flecs::entity_t ResolvedItem = FlecsSaveRemap::ResolveSaveIndex(ItemIdx);
		if (ResolvedItem != 0)
		{
			C.SlotToItemEntity.Add(SlotId, static_cast<int64>(ResolvedItem));
		}
		// If item is unresolvable (skipped due to missing prefab etc.), drop the slot
		// entry entirely — leaves the slot empty rather than pointing at entity 0.
	}

	E.set<FContainerSlotsInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FWorldItemInstance  (TypeId 0x0206, Version 1)
// DroppedByEntityId is REMAP.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Item::Encode_WorldItemInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FWorldItemInstance* C = E.try_get<FWorldItemInstance>();
	checkf(C, TEXT("Encode_WorldItemInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<float>(Ar, C->DespawnTimer);
	SaveValue<float>(Ar, C->PickupGraceTimer);

	const uint32 DroppedByIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(C->DroppedByEntityId));
	SaveValue<uint32>(Ar, DroppedByIdx);
}

bool FlecsSaveEncoders_Item::Decode_WorldItemInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_WorldItemInstance: unknown version %u"), Version);
		return false;
	}

	FWorldItemInstance C;
	Ar << C.DespawnTimer;
	Ar << C.PickupGraceTimer;

	uint32 DroppedByIdx = 0xFFFFFFFFu;
	Ar << DroppedByIdx;
	C.DroppedByEntityId = static_cast<int64>(FlecsSaveRemap::ResolveSaveIndex(DroppedByIdx));

	E.set<FWorldItemInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FContainedIn  (TypeId 0x0207, Version 1)
// ContainerEntityId is REMAP. GridPosition + SlotIndex are intrinsic.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Item::Encode_ContainedIn(const flecs::entity& E, TArray<uint8>& Out)
{
	const FContainedIn* C = E.try_get<FContainedIn>();
	checkf(C, TEXT("Encode_ContainedIn: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version

	const uint32 ContainerIdx = FlecsSaveRemap::EntityToSaveIndex(static_cast<flecs::entity_t>(C->ContainerEntityId));
	SaveValue<uint32>(Ar, ContainerIdx);
	SaveValue<int32>(Ar, C->GridPosition.X);
	SaveValue<int32>(Ar, C->GridPosition.Y);
	SaveValue<int32>(Ar, C->SlotIndex);
}

bool FlecsSaveEncoders_Item::Decode_ContainedIn(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_ContainedIn: unknown version %u"), Version);
		return false;
	}

	uint32 ContainerIdx = 0xFFFFFFFFu;
	int32 GridX = -1, GridY = -1, SlotIndex = -1;
	Ar << ContainerIdx;
	Ar << GridX;
	Ar << GridY;
	Ar << SlotIndex;

	FContainedIn C;
	C.ContainerEntityId = static_cast<int64>(FlecsSaveRemap::ResolveSaveIndex(ContainerIdx));
	C.GridPosition = FIntPoint(GridX, GridY);
	C.SlotIndex = SlotIndex;

	E.set<FContainedIn>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FMagazineInstance  (TypeId 0x0208, Version 1)
// Variable-length LIFO: write int32 AmmoCount then AmmoCount × uint8 slot entries.
// AmmoSlots is a fixed array but only the first AmmoCount entries carry data.
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Item::Encode_MagazineInstance(const flecs::entity& E, TArray<uint8>& Out)
{
	const FMagazineInstance* C = E.try_get<FMagazineInstance>();
	checkf(C, TEXT("Encode_MagazineInstance: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));
	checkf(C->AmmoCount >= 0 && C->AmmoCount <= MAX_MAGAZINE_CAPACITY,
		TEXT("Encode_MagazineInstance: entity %llu has AmmoCount=%d outside [0, %d]"),
		static_cast<uint64>(E.id()), C->AmmoCount, MAX_MAGAZINE_CAPACITY);

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<int32>(Ar, C->AmmoCount);
	for (int32 i = 0; i < C->AmmoCount; ++i)
	{
		SaveValue<uint8>(Ar, C->AmmoSlots[i]);
	}
}

bool FlecsSaveEncoders_Item::Decode_MagazineInstance(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_MagazineInstance: unknown version %u"), Version);
		return false;
	}

	int32 AmmoCount = 0;
	Ar << AmmoCount;
	if (AmmoCount < 0 || AmmoCount > MAX_MAGAZINE_CAPACITY)
	{
		UE_LOG(LogFlecsSave, Error,
			TEXT("Decode_MagazineInstance: corrupt AmmoCount=%d (max %d)"),
			AmmoCount, MAX_MAGAZINE_CAPACITY);
		return false;
	}

	FMagazineInstance C;
	C.AmmoCount = AmmoCount;
	for (int32 i = 0; i < AmmoCount; ++i)
	{
		uint8 Slot = 0;
		Ar << Slot;
		C.AmmoSlots[i] = Slot;
	}
	// Slots [AmmoCount, MAX_MAGAZINE_CAPACITY) retain default 0 from FMagazineInstance().

	E.set<FMagazineInstance>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// FAmmoTypeRef  (TypeId 0x0209, Version 1)
// ═══════════════════════════════════════════════════════════════

void FlecsSaveEncoders_Item::Encode_AmmoTypeRef(const flecs::entity& E, TArray<uint8>& Out)
{
	const FAmmoTypeRef* C = E.try_get<FAmmoTypeRef>();
	checkf(C, TEXT("Encode_AmmoTypeRef: entity %llu lacks the component"),
		static_cast<uint64>(E.id()));

	FMemoryWriter Ar(Out, true);
	Ar.SetIsSaving(true);

	SaveValue<uint16>(Ar, 1); // Version
	SaveValue<int32>(Ar, C->AmmoTypeIndex);
}

bool FlecsSaveEncoders_Item::Decode_AmmoTypeRef(const flecs::entity& E, const uint8* Bytes, uint32 Len)
{
	FMemoryReaderView Ar(TArrayView<const uint8>(Bytes, Len), true);
	Ar.SetIsLoading(true);

	uint16 Version = 0; Ar << Version;
	if (Version != 1)
	{
		UE_LOG(LogFlecsSave, Error, TEXT("Decode_AmmoTypeRef: unknown version %u"), Version);
		return false;
	}

	FAmmoTypeRef C;
	Ar << C.AmmoTypeIndex;
	E.set<FAmmoTypeRef>(C);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// AUTO-REGISTRATION
// ═══════════════════════════════════════════════════════════════

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_ItemInstance, FItemInstance, 1,
	FlecsSaveEncoders_Item::Encode_ItemInstance,
	FlecsSaveEncoders_Item::Decode_ItemInstance)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_ItemUniqueData, FItemUniqueData, 1,
	FlecsSaveEncoders_Item::Encode_ItemUniqueData,
	FlecsSaveEncoders_Item::Decode_ItemUniqueData)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_ItemTags, FItemTags, 1,
	FlecsSaveEncoders_Item::Encode_ItemTags,
	FlecsSaveEncoders_Item::Decode_ItemTags)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_ContainerInstance, FContainerInstance, 1,
	FlecsSaveEncoders_Item::Encode_ContainerInstance,
	FlecsSaveEncoders_Item::Decode_ContainerInstance)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_ContainerGridInstance, FContainerGridInstance, 1,
	FlecsSaveEncoders_Item::Encode_ContainerGridInstance,
	FlecsSaveEncoders_Item::Decode_ContainerGridInstance)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_ContainerSlotsInstance, FContainerSlotsInstance, 1,
	FlecsSaveEncoders_Item::Encode_ContainerSlotsInstance,
	FlecsSaveEncoders_Item::Decode_ContainerSlotsInstance)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_WorldItemInstance, FWorldItemInstance, 1,
	FlecsSaveEncoders_Item::Encode_WorldItemInstance,
	FlecsSaveEncoders_Item::Decode_WorldItemInstance)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_ContainedIn, FContainedIn, 1,
	FlecsSaveEncoders_Item::Encode_ContainedIn,
	FlecsSaveEncoders_Item::Decode_ContainedIn)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_MagazineInstance, FMagazineInstance, 1,
	FlecsSaveEncoders_Item::Encode_MagazineInstance,
	FlecsSaveEncoders_Item::Decode_MagazineInstance)

REGISTER_SAVE_COMPONENT(
	FlecsSaveTypeIds::kTypeId_AmmoTypeRef, FAmmoTypeRef, 1,
	FlecsSaveEncoders_Item::Encode_AmmoTypeRef,
	FlecsSaveEncoders_Item::Decode_AmmoTypeRef)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagItem, FTagItem)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagPickupable, FTagPickupable)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagContainer, FTagContainer)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagMagazine, FTagMagazine)

REGISTER_SAVE_TAG(
	FlecsSaveTypeIds::kTypeId_TagQuickLoadDevice, FTagQuickLoadDevice)
