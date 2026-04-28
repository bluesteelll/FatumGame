// Multiblock assembly blueprint — Phase 2 of the Crafting System.
//
// Describes an anchor part + N child parts (1..15) with spatial offsets from
// the anchor, which the MultiblockDetectionSystem uses to match placed parts
// in a level. On match BondMultiblock strips pickup tags, freezes bodies, and
// upgrades the anchor into a crafting station via SetupStationInstance.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FlecsCraftingTypes.h"
#include "FlecsMultiblockBlueprint.generated.h"

class UFlecsEntityDefinition;
class UFlecsCraftingStationProfile;

// ═══════════════════════════════════════════════════════════════
// CHILD PART SPEC (per-child entry inside a blueprint)
// ═══════════════════════════════════════════════════════════════

/**
 * One child part requirement in a multiblock blueprint.
 *
 * MVP caveat (R3): RelativeOffset is applied as a WORLD offset from the anchor
 * position — anchor rotation is IGNORED. Works fine for axis-aligned cube
 * assemblies. Phase 3+ will use AnchorQuat.RotateVector(RelativeOffset).
 */
USTRUCT(BlueprintType)
struct FATUMGAME_API FMultiblockChildPartSpec
{
	GENERATED_BODY()

	/** Semantic role (Firebox, Chimney, etc). Must match child entity's
	 *  FMultiblockPartStatic::PartRole at detection time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multiblock")
	FName PartRole;

	/** The UFlecsEntityDefinition this child slot accepts. Detection rejects
	 *  parts whose FEntityDefinitionRef doesn't match. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multiblock")
	TObjectPtr<UFlecsEntityDefinition> PartDefinition;

	/** Expected offset from anchor, WORLD-space (cm). Phase 2 MVP applies this
	 *  without rotating by anchor orientation — designer lays parts along a
	 *  global-axis cross around the anchor (Z stacks work out of the box). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multiblock",
		meta = (Tooltip = "Phase 2 MVP: applied as WORLD offset; anchor rotation ignored."))
	FVector RelativeOffset = FVector::ZeroVector;

	/** Max positional error from expected offset (cm). Best candidate within
	 *  this radius wins; a candidate just outside counts as a near-miss when
	 *  debug logging is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multiblock",
		meta = (ClampMin = "0.1"))
	float PositionTolerance = 10.f;

	/** Phase 2 MVP: ignored — parts are never rotation-checked. Exposed so
	 *  authoring can plan ahead for Phase 3+. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multiblock",
		meta = (ClampMin = "0.0", ClampMax = "180.0",
		        Tooltip = "Phase 2 MVP: rotation tolerance IGNORED."))
	float RotationToleranceDegrees = 180.f;

	/** Phase 4 — true = hot-swappable via wrench when station is Idle/Disabled.
	 *  False = rigid (like the anchor) — wrench-detach rejected with Warning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multiblock",
		meta = (Tooltip = "Phase 4: hot-swappable via wrench when station Idle/Disabled. False = rigid."))
	bool bSwappable = false;
};

// ═══════════════════════════════════════════════════════════════
// PHASE 4 — EXTENSION PORT (designer-authored optional socket)
// ═══════════════════════════════════════════════════════════════

/**
 * One extension port on a multiblock blueprint. Empty by default; player attaches
 * a compatible part via wrench. Effect (extra slot, fuel ceiling bump, etc.)
 * is summed into the station's effective layout via RecomputeEffectiveLayout.
 *
 * V2 PATCH 1: each port reserves slot index BaselineCount + PortIndex regardless
 * of port type — slot identity is by-port-index, NOT contiguous packing.
 */
USTRUCT(BlueprintType)
struct FATUMGAME_API FMultiblockExtensionPort
{
	GENERATED_BODY()

	/** Identifier for logs / future save-restore. */
	UPROPERTY(EditAnywhere, Category = "Port")
	FName PortId;

	/** Port type — drives the runtime extension effect (DieSlot etc.). */
	UPROPERTY(EditAnywhere, Category = "Port")
	EExtensionPortType PortType = EExtensionPortType::None;

	/** Local offset from anchor (cm) — rotated by AnchorYawSnappedDeg at attach time. */
	UPROPERTY(EditAnywhere, Category = "Port")
	FVector RelativeOffset = FVector::ZeroVector;

	/** Which PartRoles the port accepts. Wrench-attach validates against this. */
	UPROPERTY(EditAnywhere, Category = "Port")
	TArray<FName> AcceptedPartRoles;

	/** Stack cap for ports of this PortType on this blueprint (designer-set per port). */
	UPROPERTY(EditAnywhere, Category = "Port", meta = (ClampMin = "1", ClampMax = "8"))
	uint8 MaxStackedAtThisPort = 1;
};

// ═══════════════════════════════════════════════════════════════
// MULTIBLOCK BLUEPRINT (UDataAsset)
// ═══════════════════════════════════════════════════════════════

/**
 * A multiblock assembly recipe.
 *
 * Attach to the anchor UFlecsEntityDefinition via MultiblockBlueprint — and
 * also to every CHILD entity definition that participates — so every live
 * part can reach the shared spec via FMultiblockPartStatic::Blueprint.
 *
 * The anchor EntityDefinition MUST NOT have a direct CraftingStationProfile —
 * the upgrade path attaches the profile referenced here. IsDataValid enforces
 * this + children-count + offset/radius consistency.
 */
UCLASS(BlueprintType)
class FATUMGAME_API UFlecsMultiblockBlueprint : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// IDENTITY
	// ═══════════════════════════════════════════════════════════════

	/** Identifier for logs / future save-restore. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FName BlueprintId = TEXT("MB_Generic");

	// ═══════════════════════════════════════════════════════════════
	// SCHEMA
	// ═══════════════════════════════════════════════════════════════

	/** The entity definition that acts as the anchor — must itself reference
	 *  this blueprint via MultiblockBlueprint + bMultiblockIsAnchor=true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schema")
	TObjectPtr<UFlecsEntityDefinition> AnchorPartDefinition;

	/** Station profile attached to the anchor after successful bond. Inline
	 *  (Instanced) so designers can author bespoke slot layouts per blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Schema")
	TObjectPtr<UFlecsCraftingStationProfile> StationProfile;

	/** Child part requirements (1..15). Bond is all-or-nothing — missing any
	 *  child keeps the anchor unbonded (and re-scannable). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schema")
	TArray<FMultiblockChildPartSpec> Children;

	/** Radius (cm) of the detection sphere-search around the anchor. Must be
	 *  large enough to reach every child's max offset + tolerance + 10cm slack.
	 *  IsDataValid enforces this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schema",
		meta = (ClampMin = "10.0"))
	float DetectionScanRadius = 300.f;

	/** Phase 4 — optional extension ports. Empty by default; players wrench-attach
	 *  parts to occupy them. NOT scanned during initial bond. Max 8 ports. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schema",
		meta = (Tooltip = "Phase 4 extension ports — empty by default, attach via wrench."))
	TArray<FMultiblockExtensionPort> ExtensionPorts;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
