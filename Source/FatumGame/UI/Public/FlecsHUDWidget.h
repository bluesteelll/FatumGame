// C++ base class for UMG HUD widget.
//
// Subscribes to message channels, filters by entity ID, forwards to
// BlueprintImplementableEvents. Blueprint child (WBP_MainHUD) handles visuals.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"
#include "SkeletonTypes.h"
#include "FlecsHUDWidget.generated.h"

UCLASS(Abstract, Blueprintable)
class FATUMGAME_API UFlecsHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// PUBLIC API (called by AFlecsCharacter)
	// ═══════════════════════════════════════════════════════════════

	/** Set player entity ID for message filtering. */
	void SetPlayerEntityId(int64 InEntityId);

	/** Set weapon entity ID for ammo/reload filtering. */
	void SetWeaponEntityId(int64 InWeaponEntityId);

	// ═══════════════════════════════════════════════════════════════
	// BLUEPRINT IMPLEMENTABLE EVENTS
	// ═══════════════════════════════════════════════════════════════

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnHealthChanged(float CurrentHP, float MaxHP, float Percent);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnPlayerDeath();

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnAmmoChanged(int32 CurrentAmmo, int32 MagazineSize, int32 ReserveAmmo);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnReloadStarted();

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnReloadFinished(int32 NewAmmo, int32 MagazineSize);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnInteractionChanged(bool bHasTarget, const FText& Prompt, uint8 InteractionType, float HoldDuration);

	/** Hold interaction progress (0-1). bFinished true on cancel or complete. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnHoldProgress(float Progress, float TotalDuration, bool bFinished, bool bCompleted);

	/** Interaction state changed (EInteractionState cast). */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnInteractionStateChanged(uint8 State);

	/** Resource pools updated. Called from game thread poll (every tick with changes).
	 *  ResourceType: 1=Mana, 2=Stamina, 3=Energy, 4=Rage */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnResourcesUpdated(const TArray<FResourceBarData>& Resources);

	/** Mana changed. Convenience event fired per-tick when mana pool value changes. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnManaChanged(float Current, float Max, float Percent);

	/** Vitals updated (hunger, thirst, warmth). Values are 0.0–1.0 ratios. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnVitalsUpdated(float Hunger, float Thirst, float Warmth);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	// ═══════════════════════════════════════════════════════════════
	// MESSAGE HANDLERS
	// ═══════════════════════════════════════════════════════════════

	void HandleHealth(FGameplayTag Channel, const FUIHealthMessage& Msg);
	void HandleDeath(FGameplayTag Channel, const FUIDeathMessage& Msg);
	void HandleAmmo(FGameplayTag Channel, const FUIAmmoMessage& Msg);
	void HandleReload(FGameplayTag Channel, const FUIReloadMessage& Msg);
	void HandleInteraction(FGameplayTag Channel, const FUIInteractionMessage& Msg);
	void HandleHoldProgress(FGameplayTag Channel, const FUIHoldProgressMessage& Msg);
	void HandleInteractionState(FGameplayTag Channel, const FUIInteractionStateMessage& Msg);

	/** Resolve interaction prompt from EntityDefinition on game thread. */
	FText ResolveInteractionPrompt(FSkeletonKey TargetKey) const;

	// ═══════════════════════════════════════════════════════════════
	// DATA
	// ═══════════════════════════════════════════════════════════════

	/** Cached entity IDs for filtering (set by AFlecsCharacter). */
	int64 CachedPlayerEntityId = 0;
	int64 CachedWeaponEntityId = 0;

	/** Listener handles (unregistered in NativeDestruct). */
	FMessageListenerHandle HealthHandle;
	FMessageListenerHandle DeathHandle;
	FMessageListenerHandle AmmoHandle;
	FMessageListenerHandle ReloadHandle;
	FMessageListenerHandle InteractionHandle;
	FMessageListenerHandle HoldProgressHandle;
	FMessageListenerHandle InteractionStateHandle;

	/** Allow AFlecsCharacter to read CachedPlayerEntityId for one-time init check. */
	friend class AFlecsCharacter;
};
