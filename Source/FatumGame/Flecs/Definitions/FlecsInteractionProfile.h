// Interaction profile for Flecs entity spawning.
// Defines WHAT happens when the player presses E on this entity.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "FlecsInteractionTypes.h"
#include "FlecsInteractionProfile.generated.h"

class UFlecsUIPanel;

/**
 * Data Asset defining interaction behavior for an entity.
 *
 * InteractionType determines the high-level behavior:
 * - Instant: press E, immediate effect (pickup, toggle, destroy, open container, custom event)
 * - Focus:   state change with optional camera movement and UI panel (notes, keypads, containers)
 * - Hold:    hold E for duration, progress bar, then execute action
 *
 * Editor uses EditCondition to show only relevant fields for the selected type.
 */
UCLASS(BlueprintType, EditInlineNew)
class FATUMGAME_API UFlecsInteractionProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	// =================================================================
	// COMMON
	// =================================================================

	/** Text shown to the player when looking at this entity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	FText InteractionPrompt = NSLOCTEXT("Interaction", "Default", "Press E to interact");

	/** Maximum range at which this entity can be interacted with (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "50", ClampMax = "1000"))
	float InteractionRange = 300.f;

	/** If true, entity becomes non-interactable after first use */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bSingleUse = false;

	/** What happens when the player presses E */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	EInteractionType InteractionType = EInteractionType::Instant;

	/** Allow damage to cancel this interaction (Focus/Hold only) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction",
		meta = (EditCondition = "bIsNotInstant", EditConditionHides))
	bool bAllowDamageCancel = true;

	/** Camera transition time when force-cancelled by damage (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction",
		meta = (EditCondition = "bIsNotInstant && bAllowDamageCancel",
			EditConditionHides, ClampMin = "0.05", ClampMax = "0.5"))
	float DamageCancelTransitionTime = 0.15f;

	// =================================================================
	// INSTANT
	// =================================================================

	/** Specific action to execute on press */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instant",
		meta = (EditCondition = "bIsInstant", EditConditionHides))
	EInstantAction InstantAction = EInstantAction::Pickup;

	/** GameplayTag event to fire (only for CustomEvent action) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instant",
		meta = (EditCondition = "bIsInstant && bIsCustomEvent", EditConditionHides))
	FGameplayTag CustomEventTag;

	// =================================================================
	// FOCUS
	// =================================================================

	/** Whether to smoothly move camera to the focus point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus",
		meta = (EditCondition = "bIsFocus", EditConditionHides))
	bool bMoveCamera = true;

	/** UI panel to open when focused (null = no panel, just camera/state change) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus",
		meta = (EditCondition = "bIsFocus", EditConditionHides))
	TSubclassOf<UFlecsUIPanel> FocusWidgetClass;

	/** Camera position in entity local space (offset from entity center).
	 *  Rotates with the object. Example: (-80, 0, 30) = 80cm behind, 30cm above entity origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus",
		meta = (EditCondition = "bIsFocusCamera", EditConditionHides))
	FVector FocusCameraPosition = FVector(-80.f, 0.f, 30.f);

	/** Camera rotation in entity local space.
	 *  (0,0,0) = camera looks along entity's forward (+X). Rotates with the object. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus",
		meta = (EditCondition = "bIsFocusCamera", EditConditionHides))
	FRotator FocusCameraRotation = FRotator::ZeroRotator;

	/** Target FOV when focused (0 = keep current FOV) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus",
		meta = (EditCondition = "bIsFocusCamera", EditConditionHides, ClampMin = "0", ClampMax = "120"))
	float FocusFOV = 50.f;

	/** Duration of camera transition IN (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus",
		meta = (EditCondition = "bIsFocusCamera", EditConditionHides, ClampMin = "0.1", ClampMax = "2.0"))
	float TransitionInTime = 0.4f;

	/** Duration of camera transition OUT (seconds). Faster = more responsive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus",
		meta = (EditCondition = "bIsFocusCamera", EditConditionHides, ClampMin = "0.1", ClampMax = "2.0"))
	float TransitionOutTime = 0.25f;

	// =================================================================
	// HOLD
	// =================================================================

	/** How long the player must hold E (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hold",
		meta = (EditCondition = "bIsHold", EditConditionHides, ClampMin = "0.2", ClampMax = "30.0"))
	float HoldDuration = 2.f;

	/** Can the player cancel by releasing E? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hold",
		meta = (EditCondition = "bIsHold", EditConditionHides))
	bool bCanCancel = true;

	/** Action to execute when hold completes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hold",
		meta = (EditCondition = "bIsHold", EditConditionHides))
	EInstantAction CompletionAction = EInstantAction::Destroy;

	/** GameplayTag event for CustomEvent completion action */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hold",
		meta = (EditCondition = "bIsHold && bIsHoldCustomEvent", EditConditionHides))
	FGameplayTag HoldCompletionEventTag;

#if WITH_EDITORONLY_DATA
private:
	// Helper bools for EditCondition (UE can't evaluate enum == in meta reliably).
	// These are NOT serialized — computed from InteractionType via PostEditChangeProperty.

	UPROPERTY(Transient, meta = (HideInDetailPanel))
	bool bIsInstant = true;

	UPROPERTY(Transient, meta = (HideInDetailPanel))
	bool bIsNotInstant = false;

	UPROPERTY(Transient, meta = (HideInDetailPanel))
	bool bIsFocus = false;

	UPROPERTY(Transient, meta = (HideInDetailPanel))
	bool bIsHold = false;

	UPROPERTY(Transient, meta = (HideInDetailPanel))
	bool bIsFocusCamera = false;

	UPROPERTY(Transient, meta = (HideInDetailPanel))
	bool bIsCustomEvent = false;

	UPROPERTY(Transient, meta = (HideInDetailPanel))
	bool bIsHoldCustomEvent = false;

public:
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;

private:
	void RefreshEditorBools();
#endif
};
