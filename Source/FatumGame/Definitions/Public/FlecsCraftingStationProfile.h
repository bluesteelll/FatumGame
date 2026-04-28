// Crafting station profile — UDataAsset owning the per-station slot layout.
// FCraftingStationStatic stores only a const pointer to the profile (MN3 resolution);
// systems dereference SlotLayout on demand.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FlecsCraftingTypes.h"
#include "FlecsCraftingStationProfile.generated.h"

class UFlecsContainerProfile;

/**
 * Profile describing a crafting station type.
 *
 * Attach to UFlecsEntityDefinition::CraftingStationProfile to make the entity a
 * crafting station. Spawner reads SlotLayout and spawns one child container entity
 * per entry, tagging each with FCraftingSlotBackRef.
 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class FATUMGAME_API UFlecsCraftingStationProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// IDENTITY
	// ═══════════════════════════════════════════════════════════════

	/** Machine name ("Smelter_Basic"). Copied into FCraftingStationStatic::StationName. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FName StationName = TEXT("Station_Generic");

	/** Human-readable display name (debug UI / future player UI). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FText DisplayName;

	/** Station type enum — selects which recipe bucket to match against. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	ECraftingStationType StationType = ECraftingStationType::Generic;

	/** Gameplay tag for recipe gating (e.g. Station.Smelter). Optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FGameplayTag StationTag;

	// ═══════════════════════════════════════════════════════════════
	// FUEL
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Which fuel types the station accepts. Must be non-empty iff SlotLayout contains a Fuel slot.
	 * Flattened into FCraftingStationStatic::SupportedFuelMask at prefab creation time.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fuel")
	TSet<EFuelType> SupportedFuelTypes;

	// ═══════════════════════════════════════════════════════════════
	// SLOT LAYOUT
	// ═══════════════════════════════════════════════════════════════

	/**
	 * Fixed slot layout. One child container entity is spawned per entry.
	 * Rules enforced by IsDataValid + FromProfile checkf:
	 *  - Num >= 1
	 *  - >= 1 MaterialInput role
	 *  - <= 1 Fuel role
	 *  - >= 1 Output role
	 *  - Num <= kMaxCraftingSlots (24)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slots")
	TArray<FSlotLayoutDef> SlotLayout;

	// ═══════════════════════════════════════════════════════════════
	// PHASE 4 — EXTENSIONS (templates for runtime-added ports)
	// ═══════════════════════════════════════════════════════════════

	/** Phase 4 — slot template used when an extension port adds a Die slot.
	 *  Required if any ExtensionPort has PortType == DieSlot. IsDataValid enforces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Extensions")
	TObjectPtr<UFlecsContainerProfile> ExtensionDieSlotProfile;

	/** Phase 4 — slot template used when an extension port adds an Output slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Extensions")
	TObjectPtr<UFlecsContainerProfile> ExtensionOutputSlotProfile;

	/** Phase 4 — slot template used when an extension port adds a Tool slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Extensions")
	TObjectPtr<UFlecsContainerProfile> ExtensionToolSlotProfile;

	/** Phase 4 — impulse magnitude (cm/s) applied to detached parts on wrench-detach
	 *  or full deconstruct. Designer-tunable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extensions",
		meta = (ClampMin = "0", ClampMax = "200"))
	float DeconstructImpulseCmS = 50.f;

	/** Phase 4 (V2 PATCH 4) — pickup-grace seconds applied to wrench-detached parts
	 *  and deconstructed station bodies. Prevents same-tick pickup by overlapping
	 *  player capsule. Mirrors the existing player-drop grace UX. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extensions",
		meta = (ClampMin = "0", ClampMax = "5"))
	float DetachPickupGraceSeconds = 0.5f;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
