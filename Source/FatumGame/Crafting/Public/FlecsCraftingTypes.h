// Crafting domain enums + POD types + constants.
// Referenced by components, profiles, registry, runtime, and library.

#pragma once

#include "CoreMinimal.h"
#include "FlecsCraftingTypes.generated.h"

class UFlecsEntityDefinition;
class UFlecsContainerProfile;

// ═══════════════════════════════════════════════════════════════
// CONSTANTS
// ═══════════════════════════════════════════════════════════════

/** Maximum slots on a crafting station (MN1 — bumped from 16 to 24 per Phase 5 Press projection). */
static constexpr int32 kMaxCraftingSlots = 24;

// ═══════════════════════════════════════════════════════════════
// ENUMS
// ═══════════════════════════════════════════════════════════════

/** Fuel type classification — bitmask-compatible (non-None values are powers of two). */
UENUM(BlueprintType)
enum class EFuelType : uint8
{
	None          = 0   UMETA(DisplayName = "None"),
	Coal          = 1   UMETA(DisplayName = "Coal"),
	ElectricCell  = 2   UMETA(DisplayName = "Electric Cell"),
	Mana          = 4   UMETA(DisplayName = "Mana"),
};

static_assert(static_cast<uint8>(EFuelType::Coal)         == 1, "EFuelType::Coal must be 1");
static_assert(static_cast<uint8>(EFuelType::ElectricCell) == 2, "EFuelType::ElectricCell must be 2");
static_assert(static_cast<uint8>(EFuelType::Mana)         == 4, "EFuelType::Mana must be 4");

/** Crafting station type classification — bitmask-compatible (non-None values are powers of two). */
UENUM(BlueprintType)
enum class ECraftingStationType : uint8
{
	None     = 0   UMETA(DisplayName = "None"),
	Generic  = 1   UMETA(DisplayName = "Generic"),
	Smelter  = 2   UMETA(DisplayName = "Smelter"),
	Press    = 4   UMETA(DisplayName = "Press"),
	Forge    = 8   UMETA(DisplayName = "Forge"),
	Alchemy  = 16  UMETA(DisplayName = "Alchemy"),
};

/** Slot role within a crafting station's fixed layout. */
UENUM(BlueprintType)
enum class ESlotRole : uint8
{
	MaterialInput  UMETA(DisplayName = "Material Input"),
	Fuel           UMETA(DisplayName = "Fuel"),
	Output         UMETA(DisplayName = "Output"),
	Die            UMETA(DisplayName = "Die"),
	Tool           UMETA(DisplayName = "Tool"),
	Internal       UMETA(DisplayName = "Internal"),
	MAX            UMETA(Hidden),
};

/** Recipe match diagnostic published via snapshot for UI hints. */
UENUM(BlueprintType)
enum class ECraftingMatchDiagnostic : uint8
{
	None                 UMETA(DisplayName = "None"),
	NoMatch              UMETA(DisplayName = "No Match"),
	PartialIngredients   UMETA(DisplayName = "Partial Ingredients"),
	WrongStation         UMETA(DisplayName = "Wrong Station"),
	WrongFuel            UMETA(DisplayName = "Wrong Fuel"),
	InsufficientFuel     UMETA(DisplayName = "Insufficient Fuel"),
	MultipleMatches      UMETA(DisplayName = "Multiple Matches"),
};

/**
 * Smelter (and forthcoming Press / Forge) process state.
 * Lives on FSmelterInstance::Phase. Published per-tick into
 * FCraftingStationSharedState::ProcessPhasePacked atomic + FCraftingStationSnapshot::ProcessPhase.
 *
 * Cancelled and Completing are one-tick TRANSIENT phases — the SmelterProcessSystem
 * sets them, the same-tick CraftingSnapshotFlushSystem observes/publishes them, then
 * the next tick the system body transitions back to Idle. UI can therefore detect
 * "completion" / "cancellation" events by polling the published phase.
 */
UENUM(BlueprintType)
enum class EProcessPhase : uint8
{
	Idle       = 0  UMETA(DisplayName = "Idle"),
	Processing = 1  UMETA(DisplayName = "Processing"),
	Stalled    = 2  UMETA(DisplayName = "Stalled"),
	Completing = 3  UMETA(DisplayName = "Completing"),
	Cancelled  = 4  UMETA(DisplayName = "Cancelled"),
	Disabled   = 5  UMETA(DisplayName = "Disabled"),    // Phase 4 — required functional missing (modular stations)
};

// ═══════════════════════════════════════════════════════════════
// PHASE 4 — MODULAR STATIONS (extension ports + wrench)
// ═══════════════════════════════════════════════════════════════

/**
 * Type of an extension port on a multiblock blueprint. Drives runtime layout
 * deltas (extra slots, fuel ceiling, etc.) when occupied.
 */
UENUM(BlueprintType)
enum class EExtensionPortType : uint8
{
	None        = 0  UMETA(DisplayName = "None"),         // Sentinel — never used in production
	DieSlot     = 1  UMETA(DisplayName = "Die Slot"),     // +1 Die slot in effective layout
	FuelTank    = 2  UMETA(DisplayName = "Fuel Tank"),    // +60s ceiling on FuelChargeSecondsRemaining
	OutputTray  = 3  UMETA(DisplayName = "Output Tray"),  // +1 Output slot
	ToolRack    = 4  UMETA(DisplayName = "Tool Rack"),    // +1 Tool slot
	Generic     = 5  UMETA(DisplayName = "Generic"),      // Designer-defined effect (placeholder)
};

/**
 * Game-thread enum, written by PerformInteractionTrace, read by widget hooks.
 * Drives wrench-aware visual feedback (cyan/yellow/red glow) and input routing.
 * Sim thread NEVER touches this.
 */
UENUM(BlueprintType)
enum class EWrenchHoverKind : uint8
{
	NotApplicable        = 0  UMETA(DisplayName = "Not Applicable"),
	Anchor               = 1  UMETA(DisplayName = "Anchor (Rigid)"),
	SwappableFunctional  = 2  UMETA(DisplayName = "Swappable Functional"),
	RigidFunctional      = 3  UMETA(DisplayName = "Rigid Functional"),
	EmptyExtensionPort   = 4  UMETA(DisplayName = "Empty Extension Port"),
	OccupiedExtensionPort= 5  UMETA(DisplayName = "Occupied Extension Port"),
	ConnectorSegment     = 6  UMETA(DisplayName = "Connector Segment"),  // Phase 5a
};

// ═══════════════════════════════════════════════════════════════
// PHASE 5a — CONNECTOR TOPOLOGY (transport graph)
// ═══════════════════════════════════════════════════════════════

/** Phase 5a — connector entity-definition transport role. Authored on UFlecsEntityDefinition. */
UENUM(BlueprintType)
enum class EConnectorTransportRole : uint8
{
	None    = 0  UMETA(DisplayName = "None"),
	Liquid  = 1  UMETA(DisplayName = "Liquid (Trough)"),
	Power   = 2  UMETA(DisplayName = "Power (Wire)"),
};

/** Phase 5a — port direction kind on a station. Stored in FStationPorts.Ports[].Kind. */
UENUM(BlueprintType)
enum class EPortKind : uint8
{
	None         = 0  UMETA(DisplayName = "None"),
	LiquidOutlet = 1  UMETA(DisplayName = "Liquid Outlet"),
	LiquidInlet  = 2  UMETA(DisplayName = "Liquid Inlet"),
	PowerOutlet  = 3  UMETA(DisplayName = "Power Outlet"),
	PowerInlet   = 4  UMETA(DisplayName = "Power Inlet"),
};

/** Phase 5a — network resource classification. One value per FCraftingTransportNetwork. */
UENUM(BlueprintType)
enum class ETransportKind : uint8
{
	None   = 0  UMETA(DisplayName = "None"),
	Liquid = 1  UMETA(DisplayName = "Liquid"),
	Power  = 2  UMETA(DisplayName = "Power"),
};

/** Phase 5a — connector placement rejection reasons (logged at sim re-validation). */
UENUM(BlueprintType)
enum class EConnectorPlacementResult : uint8
{
	Accepted              = 0  UMETA(DisplayName = "Accepted"),
	Rejected_NoSnap       = 1  UMETA(DisplayName = "No Snap"),
	Rejected_KindMismatch = 2  UMETA(DisplayName = "Kind Mismatch"),
	Rejected_LOSBlocked   = 3  UMETA(DisplayName = "LOS Blocked"),
	Rejected_PortOccupied = 4  UMETA(DisplayName = "Port Occupied"),
	Rejected_StationBusy  = 5  UMETA(DisplayName = "Station Busy"),
	Rejected_NetworkOverflow = 6  UMETA(DisplayName = "Network Overflow"),
	Rejected_DeadTarget   = 7  UMETA(DisplayName = "Dead Target"),
};

// Phase 5a constants — bounds + tunables (cite: V1 §1.3 + V2 PATCH 1/8).
constexpr uint32 kMaxNetworksPerScene             = 32;
constexpr uint32 kMaxSegmentsPerNetwork           = 64;
constexpr uint32 kMaxNodesPerRebuildPass          = 4096;       // V2 PATCH 8 hard cap
constexpr float  kSnapToleranceCm                 = 25.f;
constexpr float  kSnapToleranceDeg                = 30.f;
constexpr uint64 kPendingConnectorTimeoutTicks    = 60;         // V2 PATCH 1
constexpr float  kConnectorPreviewSphereRadiusCm  = 30.f;       // game-thread SphereCast radius

// ═══════════════════════════════════════════════════════════════
// RECIPE INPUT / OUTPUT
// ═══════════════════════════════════════════════════════════════

/**
 * Single ingredient requirement on a recipe.
 *
 * Two fields: soft for editor/asset persistence, hard for runtime after pre-resolution
 * (MJ4 — registry calls LoadSynchronous at GameInstance::Initialize on the game thread;
 *  sim thread reads only ResolvedDefinition and never touches the soft pointer).
 */
USTRUCT(BlueprintType)
struct FATUMGAME_API FCraftingIngredient
{
	GENERATED_BODY()

	/** Soft reference — editor-authored. Registry resolves to a hard ref at Initialize(). */
	UPROPERTY(EditAnywhere, Category = "Ingredient")
	TSoftObjectPtr<UFlecsEntityDefinition> IngredientDefinition;

	/** Hard reference — filled by UFlecsCraftingRecipeRegistry::PreResolveAllIngredients. Runtime-only. */
	UPROPERTY(Transient)
	TObjectPtr<UFlecsEntityDefinition> ResolvedDefinition;

	/** How many of this ingredient are required per craft. */
	UPROPERTY(EditAnywhere, Category = "Ingredient", meta = (ClampMin = "1"))
	int32 Count = 1;
};

/**
 * Single output produced by a recipe.
 */
USTRUCT(BlueprintType)
struct FATUMGAME_API FCraftingOutput
{
	GENERATED_BODY()

	/** Soft reference — editor-authored. Registry resolves to a hard ref at Initialize(). */
	UPROPERTY(EditAnywhere, Category = "Output")
	TSoftObjectPtr<UFlecsEntityDefinition> OutputDefinition;

	/** Hard reference — filled by UFlecsCraftingRecipeRegistry::PreResolveAllIngredients. Runtime-only. */
	UPROPERTY(Transient)
	TObjectPtr<UFlecsEntityDefinition> ResolvedDefinition;

	/** How many units are produced per craft. */
	UPROPERTY(EditAnywhere, Category = "Output", meta = (ClampMin = "1"))
	int32 Count = 1;
};

// ═══════════════════════════════════════════════════════════════
// SLOT LAYOUT DEFINITION
// ═══════════════════════════════════════════════════════════════

/**
 * One slot definition within a station profile's fixed layout.
 * Stored on UFlecsCraftingStationProfile::SlotLayout — a UDataAsset TArray, safe
 * from archetype migration (MN3 — FCraftingStationStatic stores only the profile
 * pointer; systems dereference Profile->SlotLayout on demand).
 */
USTRUCT(BlueprintType)
struct FATUMGAME_API FSlotLayoutDef
{
	GENERATED_BODY()

	/** Role classification for matching and UI labelling. */
	UPROPERTY(EditAnywhere, Category = "Slot")
	ESlotRole Role = ESlotRole::MaterialInput;

	/** Editor/debug label. */
	UPROPERTY(EditAnywhere, Category = "Slot")
	FName SlotName;

	/** Container profile describing capacity/layout/filter for this slot's backing container entity.
	 *  Instanced — can be either a shared DA reference OR created inline via "+ Add" in the editor
	 *  (no need to author a dedicated UFlecsContainerProfile DA per slot). */
	UPROPERTY(EditAnywhere, Instanced, Category = "Slot")
	TObjectPtr<UFlecsContainerProfile> ContainerProfile;

	/** If true, player cannot drag items directly in/out (Phase 3 will enforce via FlecsContainerLibrary). */
	UPROPERTY(EditAnywhere, Category = "Slot")
	bool bReadOnlyFromPlayer = false;
};
