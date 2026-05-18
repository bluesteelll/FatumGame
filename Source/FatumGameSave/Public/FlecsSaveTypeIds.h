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
	// 0x10xx — CORE TAGS
	// ═══════════════════════════════════════════════════════════════

	/** FTagInteractable — entity can be E-pressed by player. */
	inline constexpr uint16 kTypeId_TagInteractable        = 0x1000;
}
