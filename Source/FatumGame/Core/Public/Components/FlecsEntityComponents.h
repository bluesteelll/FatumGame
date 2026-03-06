// Entity-level components: definition reference, focus camera, loot.

#pragma once

#include "CoreMinimal.h"

class UFlecsEntityDefinition;

// ═══════════════════════════════════════════════════════════════
// ENTITY DEFINITION REFERENCE
// ═══════════════════════════════════════════════════════════════

struct FEntityDefinitionRef
{
	/** Reference to the EntityDefinition asset */
	UFlecsEntityDefinition* Definition = nullptr;

	bool IsValid() const { return Definition != nullptr; }
};

// ═══════════════════════════════════════════════════════════════
// FOCUS CAMERA OVERRIDE (per-instance)
// ═══════════════════════════════════════════════════════════════

/**
 * Per-instance override for focus camera position/rotation.
 * Set by AFlecsEntitySpawner when the level designer wants a custom viewpoint.
 * Values are in entity local space (rotates with the object).
 *
 * If present on entity, used INSTEAD of InteractionProfile defaults.
 */
struct FFocusCameraOverride
{
	/** Camera position in entity local space */
	FVector CameraPosition = FVector::ZeroVector;

	/** Camera rotation in entity local space */
	FRotator CameraRotation = FRotator::ZeroRotator;
};

// ═══════════════════════════════════════════════════════════════
// LOOT STATIC
// ═══════════════════════════════════════════════════════════════

/**
 * Static loot data - lives in PREFAB.
 * Defines what drops when entity dies. No instance component needed.
 */
struct FLootStatic
{
	/** Minimum number of drops */
	int32 MinDrops = 1;

	/** Maximum number of drops */
	int32 MaxDrops = 3;

	/** Loot table ID (for future loot table system) */
	int32 LootTableId = 0;

	/** Drop chance (0.0 - 1.0, 1.0 = always drops) */
	float DropChance = 1.f;
};
