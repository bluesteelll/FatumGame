// Static components for Flecs Prefabs.
// These components store immutable data shared by all entities of the same type.
// They live in PREFAB entities and are inherited via IsA relationship.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "FlecsGameTags.h"

class UFlecsItemDefinition;
class UFlecsEntityDefinition;

// ═══════════════════════════════════════════════════════════════
// PREFAB ARCHITECTURE
// ═══════════════════════════════════════════════════════════════
//
// Static components are stored once per entity TYPE in a prefab.
// Instance components are stored per ENTITY.
//
// Example:
//   PREFAB "EnemyGoblin":
//     FHealthStatic { MaxHP=100, Armor=5 }
//     FDamageStatic { Damage=10 }
//
//   ENTITY (each goblin):
//     IsA -> EnemyGoblin (inherits static data)
//     FHealthInstance { CurrentHP=75 }  <- mutable per-entity
//
// Lookup:
//   const FHealthStatic* Static = Entity.try_get<FHealthStatic>();  // from prefab
//   FHealthInstance* Instance = Entity.get_mut<FHealthInstance>();   // from entity
// ═══════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════
// HEALTH STATIC
// ═══════════════════════════════════════════════════════════════

/**
 * Static health data - lives in PREFAB, shared by all entities of this type.
 * Contains immutable health rules.
 *
 * Instance data (CurrentHP) is in FHealthInц stance.
 */
struct FHealthStatic
{
	/** Maximum hit points */
	float MaxHP = 100.f;

	/** Damage reduction (flat) */
	float Armor = 0.f;

	/** HP regeneration per second (0 = no regen) */
	float RegenPerSecond = 0.f;

	/** Should entity be destroyed when HP reaches 0? */
	bool bDestroyOnDeath = true;

	/** Starting HP ratio (1.0 = full health) */
	float StartingHPRatio = 1.f;

	float GetStartingHP() const { return MaxHP * StartingHPRatio; }
};

// ═══════════════════════════════════════════════════════════════
// DAMAGE STATIC
// ═══════════════════════════════════════════════════════════════

/**
 * Static damage data - lives in PREFAB, shared by all entities of this type.
 * Contains immutable damage rules. No instance component needed.
 */
struct FDamageStatic
{
	/** Base damage dealt on contact */
	float Damage = 10.f;

	/** Damage type tag for resistance/weakness */
	FGameplayTag DamageType;

	/** Is this area damage? */
	bool bAreaDamage = false;

	/** Area damage radius (only if bAreaDamage) */
	float AreaRadius = 0.f;

	/** Should the entity be destroyed after dealing damage? */
	bool bDestroyOnHit = false;

	/** Critical hit chance (0.0 - 1.0) */
	float CritChance = 0.f;

	/** Critical hit damage multiplier */
	float CritMultiplier = 2.f;
};

// ═══════════════════════════════════════════════════════════════
// PROJECTILE STATIC
// ═══════════════════════════════════════════════════════════════

/**
 * Static projectile data - lives in PREFAB, shared by all projectiles of this type.
 * Contains immutable projectile rules.
 *
 * Instance data (LifetimeRemaining, CurrentBounces) is in FProjectileInstance.
 */
struct FProjectileStatic
{
	/** Maximum lifetime in seconds */
	float MaxLifetime = 10.f;

	/** Max bounces before destruction (-1 = infinite) */
	int32 MaxBounces = -1;

	/** Grace period frames after spawn/bounce before velocity check */
	int32 GracePeriodFrames = 30; // ~0.25 sec at 120Hz

	/** Minimum velocity before projectile is killed (units/sec) */
	float MinVelocity = 50.f;

	/** Should maintain constant speed? */
	bool bMaintainSpeed = false;

	/** Target speed if bMaintainSpeed (units/sec) */
	float TargetSpeed = 0.f;
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

// ═══════════════════════════════════════════════════════════════
// ITEM STATIC DATA
// ═══════════════════════════════════════════════════════════════

/**
 * Static item data - lives in PREFAB, shared by all instances of this item type.
 * One prefab per UFlecsEntityDefinition with ItemDefinition.
 *
 * Contains immutable data that never changes per-instance:
 * - Item type identification
 * - Stacking rules
 * - Weight, grid size
 * - Reference back to EntityDefinition (for spawning in world)
 *
 * Instance data (Count) is in FItemInstance.
 */
struct FItemStaticData
{
	/** Item type ID (from UFlecsItemDefinition::ItemTypeId) */
	int32 TypeId = 0;

	/** Max stack size (0 = unique, 1 = single, >1 = stackable) */
	int32 MaxStack = 99;

	/** Item weight per unit */
	float Weight = 0.1f;

	/** Grid size for 2D inventories */
	FIntPoint GridSize = FIntPoint(1, 1);

	/** Item name for debug/display */
	FName ItemName;

	/**
	 * Reference to the full EntityDefinition.
	 * Used when dropping item to world (needs physics/render profiles).
	 * Stored as raw pointer - prefabs outlive items, Definition outlives prefab.
	 */
	UFlecsEntityDefinition* EntityDefinition = nullptr;

	/** Reference to ItemDefinition for gameplay data (actions, tags, etc) */
	UFlecsItemDefinition* ItemDefinition = nullptr;

	bool IsStackable() const { return MaxStack > 1; }
	bool IsUnique() const { return MaxStack == 0; }
};

// ═══════════════════════════════════════════════════════════════
// CONTAINER STATIC
// EContainerType enum is defined in FlecsComponents.h (UENUM for UHT)
// ═══════════════════════════════════════════════════════════════

/**
 * Static container data - lives in PREFAB, shared by all containers of this type.
 * Contains immutable container rules.
 *
 * Instance data (CurrentWeight, CurrentCount) is in FContainerInstance.
 */
struct FContainerStatic
{
	/** Container type determines layout */
	EContainerType Type = EContainerType::List;

	/** Grid dimensions (for Grid type) */
	int32 GridWidth = 10;
	int32 GridHeight = 4;

	/** Max items (for List type, -1 = unlimited) */
	int32 MaxItems = -1;

	/** Max weight (-1 = unlimited) */
	float MaxWeight = -1.f;

	/** Allow nesting containers? */
	bool bAllowNesting = true;

	/** Auto-stack same items on add? */
	bool bAutoStack = true;

	int32 GetTotalCells() const { return GridWidth * GridHeight; }
};

// ═══════════════════════════════════════════════════════════════
// ENTITY DEFINITION REFERENCE
// ═══════════════════════════════════════════════════════════════

/**
 * Reference to UFlecsEntityDefinition - lives in PREFAB.
 * Allows getting back to the UE asset from any entity.
 */
// ═══════════════════════════════════════════════════════════════
// INTERACTION STATIC
// ═══════════════════════════════════════════════════════════════

/**
 * Static interaction data - lives in PREFAB, shared by all entities of this type.
 * Contains immutable interaction rules.
 *
 * InteractionType and InstantAction are cached here from InteractionProfile
 * so the sim thread can dispatch instant actions without reading UDataAsset.
 * Focus/Hold params are game-thread only — read from FEntityDefinitionRef→InteractionProfile.
 */
struct FInteractionStatic
{
	/** Maximum interaction range (cm) */
	float MaxRange = 300.f;

	/** If true, entity becomes non-interactable after first use */
	bool bSingleUse = false;

	/** Interaction type (EInteractionType cast to uint8) */
	uint8 InteractionType = 0;

	/** Instant action type (EInstantAction cast to uint8, for Instant/Hold completion) */
	uint8 InstantAction = 0;
};

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
// DESTRUCTIBLE STATIC
// ═══════════════════════════════════════════════════════════════

class UFlecsDestructibleProfile;

/**
 * Static destructible data — lives on entity (not prefab, since it holds a UObject*).
 * Presence of this component identifies an entity as fragmentable.
 */
struct FDestructibleStatic
{
	/** Reference to the destructible profile (fragment geometry, break force, etc.) */
	UFlecsDestructibleProfile* Profile = nullptr;

	bool IsValid() const { return Profile != nullptr; }
};
