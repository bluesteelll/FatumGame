// Game-specific Flecs tags and enums.
//
// Tags are zero-size components used for archetype filtering.
// Enums are UENUMs required for UPROPERTY in DataAsset profiles.
//
// For Flecs-Barrage bridge components, see: FlecsBarrageComponents.h (FlecsBarrage plugin)
// For domain components, see: FlecsHealthComponents.h, FlecsProjectileComponents.h,
//   FlecsItemComponents.h, FlecsDestructibleComponents.h, FlecsEntityComponents.h, FlecsInteractionComponents.h

#pragma once

#include "CoreMinimal.h"
#include "FlecsGameTags.generated.h"

// ═══════════════════════════════════════════════════════════════
// ENUMS (must be UENUM for use in UPROPERTY)
// ═══════════════════════════════════════════════════════════════

/** Container type enum - determines inventory layout */
UENUM(BlueprintType)
enum class EContainerType : uint8
{
	Grid,  // 2D inventory (Diablo/Tarkov style)
	Slot,  // Named equipment slots
	List   // Simple array
};

// ═══════════════════════════════════════════════════════════════
// GAME-SPECIFIC TAGS (zero-size components for archetype filtering)
// These are not USTRUCT - they are pure C++ Flecs tags.
// ═══════════════════════════════════════════════════════════════

/** Entity is a world item (has physics, can be picked up) */
struct FTagItem {};

/** Entity is a world item that was just dropped (pickup grace period) */
struct FTagDroppedItem {};

/** Entity is a container */
struct FTagContainer {};

/** Entity can be destroyed by damage */
struct FTagDestructible {};

/** Entity can be picked up by players */
struct FTagPickupable {};

/** Entity has loot drops on death */
struct FTagHasLoot {};

/** Entity is dead (pending cleanup) */
struct FTagDead {};

/** Entity is a projectile that deals damage on contact */
struct FTagProjectile {};

/** Entity is a character/player */
struct FTagCharacter {};

/** Entity is equipment (can be equipped to slot) */
struct FTagEquipment {};

/** Entity is consumable (can be used/eaten) */
struct FTagConsumable {};

/** Entity can be interacted with (press E) */
struct FTagInteractable {};

/** Entity is a debris fragment from a destroyed object */
struct FTagDebrisFragment {};

/** Collision pair: projectile hit a fragmentable object */
struct FTagCollisionFragmentation {};

/** Entity is a door (hinged or sliding) */
struct FTagDoor {};

/** Entity is a trigger that can unlock/toggle a door */
struct FTagDoorTrigger {};

/** Entity is currently held by telekinesis (prevents double-grab) */
struct FTagTelekinesisHeld {};

/** Entity is a stealth light source (gameplay-only, not rendering) */
struct FTagStealthLight {};

/** Entity is a noise zone (modifies footstep noise by surface type) */
struct FTagNoiseZone {};

/** Entity is a temperature zone (modifies ambient temperature for vitals warmth) */
struct FTagTemperatureZone {};
