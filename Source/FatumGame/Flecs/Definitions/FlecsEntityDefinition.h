// Unified entity definition combining all profiles into one Data Asset.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsEntityDefinition.generated.h"

class UFlecsItemDefinition;
class UFlecsPhysicsProfile;
class UFlecsRenderProfile;
class UFlecsHealthProfile;
class UFlecsDamageProfile;
class UFlecsProjectileProfile;
class UFlecsContainerProfile;
class UFlecsWeaponProfile;
class UFlecsInteractionProfile;
class UFlecsNiagaraProfile;

/**
 * Unified entity definition - a preset combining multiple profiles.
 *
 * Create in Content Browser: Right Click -> Miscellaneous -> Data Asset -> FlecsEntityDefinition
 *
 * This is a convenience wrapper that combines:
 * - Item logic (stacking, actions)
 * - Physics (collision, mass)
 * - Rendering (mesh, material)
 * - Health (HP, armor)
 * - Damage (contact damage)
 * - Projectile (lifetime, bounces)
 * - Container (inventory)
 *
 * Examples:
 *   DA_HealthPotion:  Item + Physics + Render + Pickupable
 *   DA_Bullet:        Physics + Render + Projectile + Damage
 *   DA_Crate:         Physics + Render + Health + Destructible + HasLoot
 *   DA_Chest:         Physics + Render + Container
 *   DA_PlayerInventory: Container only (no world presence)
 *
 * Usage:
 *   UFlecsEntityLibrary::SpawnEntity(World, DA_HealthPotion, Location);
 */
UCLASS(BlueprintType)
class FATUMGAME_API UFlecsEntityDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// IDENTITY
	// ═══════════════════════════════════════════════════════════════

	/** Display name for this entity type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FName EntityName = "NewEntity";

	/** Description for tooltips/debugging */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity", meta = (MultiLine = true))
	FText Description;

	// ═══════════════════════════════════════════════════════════════
	// PROFILES (composition)
	// Can reference existing Data Assets OR create inline (Instanced)
	// ═══════════════════════════════════════════════════════════════

	/** Item logic - makes this an item (stacking, actions, inventory) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Profiles")
	TObjectPtr<UFlecsItemDefinition> ItemDefinition;

	/** Physics - adds collision and physics simulation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Profiles")
	TObjectPtr<UFlecsPhysicsProfile> PhysicsProfile;

	/** Rendering - adds visual mesh via ISM */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Profiles")
	TObjectPtr<UFlecsRenderProfile> RenderProfile;

	/** Health - makes entity damageable */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Profiles")
	TObjectPtr<UFlecsHealthProfile> HealthProfile;

	/** Damage - makes entity deal contact damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Profiles")
	TObjectPtr<UFlecsDamageProfile> DamageProfile;

	/** Projectile - makes entity a projectile with lifetime */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Profiles")
	TObjectPtr<UFlecsProjectileProfile> ProjectileProfile;

	/** Container - makes entity a container (inventory, chest) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Profiles")
	TObjectPtr<UFlecsContainerProfile> ContainerProfile;

	/** Weapon - makes entity a weapon with firing and ammo */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Profiles")
	TObjectPtr<UFlecsWeaponProfile> WeaponProfile;

	/** Interaction - makes entity interactable (press E) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Profiles")
	TObjectPtr<UFlecsInteractionProfile> InteractionProfile;

	/** Niagara VFX - adds visual effects (trails, death explosions) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Profiles")
	TObjectPtr<UFlecsNiagaraProfile> NiagaraProfile;

	// ═══════════════════════════════════════════════════════════════
	// DEFAULT TAGS
	// ═══════════════════════════════════════════════════════════════

	/** Can be picked up by characters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags")
	bool bPickupable = false;

	/** Can be destroyed by damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags")
	bool bDestructible = false;

	/** Drops loot on death */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags")
	bool bHasLoot = false;

	/** Is a character entity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags")
	bool bIsCharacter = false;

	// ═══════════════════════════════════════════════════════════════
	// SPAWN DEFAULTS
	// ═══════════════════════════════════════════════════════════════

	/** Default item count when spawning (for items) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Defaults", meta = (ClampMin = "1"))
	int32 DefaultItemCount = 1;

	/** Default despawn time (-1 = never) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Defaults")
	float DefaultDespawnTime = -1.f;

	// ═══════════════════════════════════════════════════════════════
	// HELPERS
	// ═══════════════════════════════════════════════════════════════

	/** Check if this definition has any profiles set */
	bool HasAnyProfile() const
	{
		return ItemDefinition != nullptr
			|| PhysicsProfile != nullptr
			|| RenderProfile != nullptr
			|| HealthProfile != nullptr
			|| DamageProfile != nullptr
			|| ProjectileProfile != nullptr
			|| ContainerProfile != nullptr
			|| WeaponProfile != nullptr
			|| InteractionProfile != nullptr
			|| NiagaraProfile != nullptr;
	}

	/** Check if this will create a world entity (physics or render) */
	bool IsWorldEntity() const
	{
		return PhysicsProfile != nullptr || RenderProfile != nullptr;
	}

	/** Check if this is an item */
	bool IsItem() const { return ItemDefinition != nullptr; }

	/** Check if this is a projectile */
	bool IsProjectile() const { return ProjectileProfile != nullptr; }

	/** Check if this is a container */
	bool IsContainer() const { return ContainerProfile != nullptr; }

	/** Check if this is a weapon */
	bool IsWeapon() const { return WeaponProfile != nullptr; }

	/** Check if this is interactable */
	bool IsInteractable() const { return InteractionProfile != nullptr; }

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId("FlecsEntityDefinition", EntityName);
	}
};
