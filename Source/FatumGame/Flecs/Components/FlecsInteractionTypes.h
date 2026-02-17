// Interaction system enums.
// Used by InteractionProfile (data asset), FInteractionStatic (ECS), and character state machine.

#pragma once

#include "CoreMinimal.h"
#include "FlecsInteractionTypes.generated.h"

/**
 * What happens when the player presses E on this entity.
 * Configured per-entity via InteractionProfile data asset.
 */
UENUM(BlueprintType)
enum class EInteractionType : uint8
{
	/** Press E, immediate effect. No state change, no camera movement. */
	Instant       UMETA(DisplayName = "Instant"),

	/** State change: cursor appears, movement blocked. Optionally moves camera, optionally opens UI panel. */
	Focus         UMETA(DisplayName = "Focus"),

	/** Hold E, progress bar fills. On completion executes an action. */
	Hold          UMETA(DisplayName = "Hold")
};

/**
 * Specific action for Instant interactions (and Hold completion).
 */
UENUM(BlueprintType)
enum class EInstantAction : uint8
{
	/** Pick up item into inventory */
	Pickup          UMETA(DisplayName = "Pickup"),

	/** Toggle on/off state */
	Toggle          UMETA(DisplayName = "Toggle"),

	/** Destroy entity (add FTagDead) */
	Destroy         UMETA(DisplayName = "Destroy"),

	/** Open container/loot UI */
	OpenContainer   UMETA(DisplayName = "Open Container"),

	/** Fire a custom GameplayTag event */
	CustomEvent     UMETA(DisplayName = "Custom Event")
};

/**
 * Character-side interaction state machine.
 * Internal to AFlecsCharacter, NOT exposed to editor.
 */
enum class EInteractionState : uint8
{
	Gameplay,
	Focusing,
	Focused,
	Unfocusing,
	Holding
};
