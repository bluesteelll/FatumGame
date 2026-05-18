// FlecsSaveTypeIds — central registry of TypeIds used on disk.
//
// Per v2 §M10: stable on-disk identifiers grouped by domain. NEVER reuse a retired id;
// always bump a new value within the same range. Range conventions:
//
//   0x0000–0x00FF — reserved / header (never registered)
//   0x0100–0x0FFF — non-tag components, grouped by domain nibble:
//                     0x01xx Core / Health / Movement / Resources / Interaction / Vitals / Stealth
//                     0x02xx Item / Container / Magazine
//                     0x03xx Weapon / Penetration / Projectile
//                     0x04xx Melee
//                     0x05xx Destructible / Door
//                     0x06xx Crafting Station / Slot / Smelter
//                     0x07xx Multiblock / Transport / Ports
//   0x1000–0x1FFF — zero-size tags, same domain-nibble convention (0x10xx Core tags, etc.)
//   0x2000–0x2FFF — pairs (relationship + target encoded inline)
//   0xFF00–0xFFFF — reserved future expansion
//
// Each encoder file imports this header and references its kTypeId_* constants in the
// REGISTER_SAVE_COMPONENT / REGISTER_SAVE_TAG macros. Phase 2 owns 0x01xx + 0x10xx ranges
// for Core domain — later phases extend additional ranges.

#pragma once

#include "CoreMinimal.h"

namespace FlecsSaveTypeIds
{
	// ═══════════════════════════════════════════════════════════════
	// 0x01xx — CORE / HEALTH / MOVEMENT / INTERACTION / RESOURCES / VITALS / STEALTH
	// ═══════════════════════════════════════════════════════════════

	/** FEntityDefinitionRef — prefab back-reference. SPECIAL: serialized as the prefab
	 *  asset path in the per-entity header, NOT as a regular component block. The id is
	 *  reserved here so the registry treats it as "known but header-only". */
	inline constexpr uint16 kTypeId_EntityDefinitionRef    = 0x0100;

	/** FFocusCameraOverride — per-instance camera viewpoint (local space). */
	inline constexpr uint16 kTypeId_FocusCameraOverride    = 0x0101;

	/** FInteractionInstance — bToggleState + UseCount. */
	inline constexpr uint16 kTypeId_InteractionInstance    = 0x0102;

	/** FInteractionAngleOverride — per-instance cone restriction. */
	inline constexpr uint16 kTypeId_InteractionAngleOverride = 0x0103;

	/** FHealthInstance — CurrentHP + RegenAccumulator. */
	inline constexpr uint16 kTypeId_HealthInstance         = 0x0104;

	/** FMovementState — Posture / MoveMode / Speed / VerticalSpeed / LeanDir. */
	inline constexpr uint16 kTypeId_MovementState          = 0x0105;

	/** FResourcePools — Pools[MAX_RESOURCE_POOLS] + PoolCount. */
	inline constexpr uint16 kTypeId_ResourcePools          = 0x0106;

	/** FVitalsInstance — hunger / thirst / warmth percent + accumulators. */
	inline constexpr uint16 kTypeId_VitalsInstance         = 0x0107;

	/** FStealthInstance — light / noise / detectability levels. */
	inline constexpr uint16 kTypeId_StealthInstance        = 0x0108;

	// ═══════════════════════════════════════════════════════════════
	// 0x02xx — ITEM / CONTAINER / MAGAZINE (Phase 3)
	// ═══════════════════════════════════════════════════════════════

	/** FItemInstance — stack count. */
	inline constexpr uint16 kTypeId_ItemInstance           = 0x0200;

	/** FItemUniqueData — unique item state (durability, enchantments, custom stats). */
	inline constexpr uint16 kTypeId_ItemUniqueData         = 0x0201;

	/** FItemTags — gameplay tag container for slot/inventory filtering. */
	inline constexpr uint16 kTypeId_ItemTags               = 0x0202;

	/** FContainerInstance — weight / count / OwnerEntityId (REMAP). */
	inline constexpr uint16 kTypeId_ContainerInstance      = 0x0203;

	/** FContainerGridInstance — occupancy bitmask for 2D grid containers. */
	inline constexpr uint16 kTypeId_ContainerGridInstance  = 0x0204;

	/** FContainerSlotsInstance — TMap<SlotId, ItemEntityId> (each value REMAP). */
	inline constexpr uint16 kTypeId_ContainerSlotsInstance = 0x0205;

	/** FWorldItemInstance — despawn / pickup-grace timers + DroppedByEntityId (REMAP). */
	inline constexpr uint16 kTypeId_WorldItemInstance      = 0x0206;

	/** FContainedIn — ContainerEntityId (REMAP) + grid/slot position. */
	inline constexpr uint16 kTypeId_ContainedIn            = 0x0207;

	/** FMagazineInstance — variable-length LIFO ammo stack. */
	inline constexpr uint16 kTypeId_MagazineInstance       = 0x0208;

	/** FAmmoTypeRef — ammo type index on loose-ammo items. */
	inline constexpr uint16 kTypeId_AmmoTypeRef            = 0x0209;

	// ═══════════════════════════════════════════════════════════════
	// 0x03xx — WEAPON / PROJECTILE / PENETRATION (Phase 3 covers WEAPON)
	// ═══════════════════════════════════════════════════════════════

	/** FWeaponInstance — magazine/reload/bloom/cycle/quickload state. CHARGE STATE ZEROED on load (v2 §5.5). */
	inline constexpr uint16 kTypeId_WeaponInstance         = 0x0300;

	/** FEquippedBy — CharacterEntityId (REMAP) + SlotId. */
	inline constexpr uint16 kTypeId_EquippedBy             = 0x0301;

	/** FWeaponSlotState — active slot, equip phase, WeaponSlotContainerId (REMAP). */
	inline constexpr uint16 kTypeId_WeaponSlotState        = 0x0302;

	// ═══════════════════════════════════════════════════════════════
	// 0x04xx — MELEE (Phase 3)
	// ═══════════════════════════════════════════════════════════════

	/** FMeleeWeaponInstance — phase / charge / hits / sweep / block. ALL CHARGE+SWING STATE ZEROED, BladeBuffer=nullptr (v2 §5.5+§5.6). */
	inline constexpr uint16 kTypeId_MeleeWeaponInstance    = 0x0400;

	/** FMeleeAttackDirectionBuffer — integrated mouse delta. Reset on load. */
	inline constexpr uint16 kTypeId_MeleeAttackDirectionBuffer = 0x0401;

	/** FBladeSocketSync — blade socket snapshot. FrameStamp reset to 0 on load. */
	inline constexpr uint16 kTypeId_BladeSocketSync        = 0x0402;

	// ═══════════════════════════════════════════════════════════════
	// 0x06xx — CRAFTING STATION / SLOT / SMELTER (Phase 4)
	// ═══════════════════════════════════════════════════════════════

	/** FCraftingStationInstance — matched recipe (NULLED on load per v2 §M6), digest cache, fuel reservoir. */
	inline constexpr uint16 kTypeId_CraftingStationInstance       = 0x0600;

	/** FCraftingSlots — fixed 24-slot table of child container entity ids (each REMAP). */
	inline constexpr uint16 kTypeId_CraftingSlots                 = 0x0601;

	/** FFuelSlot — denormalized fuel-slot container entity id (REMAP). */
	inline constexpr uint16 kTypeId_FuelSlot                      = 0x0602;

	/** FCraftingSlotBackRef — slot-side back ref. StationEntityId REMAP + slot metadata. */
	inline constexpr uint16 kTypeId_CraftingSlotBackRef           = 0x0603;

	/** FSmelterInstance — phase / progress / ingredient ledger (recipe Definition* resolved via path table). */
	inline constexpr uint16 kTypeId_SmelterInstance               = 0x0604;

	/** FCraftingSlotLockedByStation — slot-side lock with OwningStationEntityId (REMAP). */
	inline constexpr uint16 kTypeId_CraftingSlotLockedByStation   = 0x0605;

	/** FStationEffectiveLayout — derived layout deltas from extension ports (intrinsic fields only). */
	inline constexpr uint16 kTypeId_StationEffectiveLayout        = 0x0606;

	// ═══════════════════════════════════════════════════════════════
	// 0x07xx — MULTIBLOCK / TRANSPORT / PORTS (Phase 4)
	// ═══════════════════════════════════════════════════════════════

	/** FMultiblockChildren — anchor's roster of bonded child slots (each ChildEntityId REMAP). */
	inline constexpr uint16 kTypeId_MultiblockChildren            = 0x0700;

	/** FMultiblockChildOf — child's back-ref AnchorEntityId (REMAP). */
	inline constexpr uint16 kTypeId_MultiblockChildOf             = 0x0701;

	/** FMultiblockExtensions — per-station roster of attached extension parts (each REMAP). */
	inline constexpr uint16 kTypeId_MultiblockExtensions          = 0x0702;

	/** FConnectorPlaced — placed-segment snap targets (REMAP) + port indices + NetworkId
	 *  (latter regenerated by NetworkRebuildSystem on first sim tick post-load). */
	inline constexpr uint16 kTypeId_ConnectorPlaced               = 0x0703;

	/** FStationPorts — variable-length per-station ports (each ConnectedSegmentId REMAP). */
	inline constexpr uint16 kTypeId_StationPorts                  = 0x0704;

	// ═══════════════════════════════════════════════════════════════
	// 0x10xx — CORE TAGS
	// ═══════════════════════════════════════════════════════════════

	/** FTagInteractable — entity can be E-pressed by player. */
	inline constexpr uint16 kTypeId_TagInteractable        = 0x1000;

	// ═══════════════════════════════════════════════════════════════
	// 0x12xx — ITEM TAGS (Phase 3)
	// ═══════════════════════════════════════════════════════════════

	/** FTagItem — entity is a world item. */
	inline constexpr uint16 kTypeId_TagItem                = 0x1200;

	/** FTagPickupable — entity can be picked up by players. */
	inline constexpr uint16 kTypeId_TagPickupable          = 0x1201;

	/** FTagContainer — entity is a container. */
	inline constexpr uint16 kTypeId_TagContainer           = 0x1202;

	/** FTagMagazine — entity is a magazine. */
	inline constexpr uint16 kTypeId_TagMagazine            = 0x1203;

	/** FTagQuickLoadDevice — entity is a stripper clip / speedloader. */
	inline constexpr uint16 kTypeId_TagQuickLoadDevice     = 0x1204;

	// ═══════════════════════════════════════════════════════════════
	// 0x13xx — WEAPON TAGS (Phase 3)
	// ═══════════════════════════════════════════════════════════════

	/** FTagWeapon — entity is a weapon. */
	inline constexpr uint16 kTypeId_TagWeapon              = 0x1300;

	// ═══════════════════════════════════════════════════════════════
	// 0x14xx — MELEE TAGS (Phase 3)
	// ═══════════════════════════════════════════════════════════════

	/** FTagMeleeWeapon — entity is a melee weapon. */
	inline constexpr uint16 kTypeId_TagMeleeWeapon         = 0x1400;

	// ═══════════════════════════════════════════════════════════════
	// 0x16xx — CRAFTING TAGS (Phase 4)
	// ═══════════════════════════════════════════════════════════════

	/** FTagCraftingStation — marks station entities for queries. */
	inline constexpr uint16 kTypeId_TagCraftingStation     = 0x1600;

	/** FTagCraftingFuel — marks fuel-item prefab descendants. */
	inline constexpr uint16 kTypeId_TagCraftingFuel        = 0x1601;

	/** FTagStationDisabled — transient sim-side marker for required-functional-missing stations. */
	inline constexpr uint16 kTypeId_TagStationDisabled     = 0x1602;

	// ═══════════════════════════════════════════════════════════════
	// 0x17xx — MULTIBLOCK / TRANSPORT TAGS (Phase 4)
	// ═══════════════════════════════════════════════════════════════

	/** FTagMultiblockPart — prefab-side: entity type participates in multiblock assembly. */
	inline constexpr uint16 kTypeId_TagMultiblockPart      = 0x1700;

	/** FTagMultiblockAnchor — prefab-side: entity type is the anchor of its blueprint. */
	inline constexpr uint16 kTypeId_TagMultiblockAnchor    = 0x1701;

	/** FTagMultiblockBonded — instance-side: entity has been bonded into an assembled multiblock. */
	inline constexpr uint16 kTypeId_TagMultiblockBonded    = 0x1702;

	/** FTagConnectorSegment — prefab-side connector marker. */
	inline constexpr uint16 kTypeId_TagConnectorSegment    = 0x1703;

	/** FTagConnectorPlaced — instance-side: segment placed in world (vs in-inventory item). */
	inline constexpr uint16 kTypeId_TagConnectorPlaced     = 0x1704;
}
