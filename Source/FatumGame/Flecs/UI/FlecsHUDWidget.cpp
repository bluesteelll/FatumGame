// UFlecsHUDWidget: C++ base for UMG HUD.
// Subscribes to message channels, filters, forwards to BlueprintImplementableEvents.

#include "FlecsHUDWidget.h"
#include "FlecsUIMessages.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsStaticComponents.h"
#include "FlecsEntityDefinition.h"
#include "FlecsInteractionProfile.h"

// ═══════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════

void UFlecsHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UFlecsMessageSubsystem* MsgSub = UFlecsMessageSubsystem::Get(this);
	check(MsgSub);

	HealthHandle = MsgSub->RegisterListener<FUIHealthMessage>(
		TAG_UI_Health, this,
		[this](FGameplayTag Tag, const FUIHealthMessage& Msg) { HandleHealth(Tag, Msg); });

	DeathHandle = MsgSub->RegisterListener<FUIDeathMessage>(
		TAG_UI_Death, this,
		[this](FGameplayTag Tag, const FUIDeathMessage& Msg) { HandleDeath(Tag, Msg); });

	AmmoHandle = MsgSub->RegisterListener<FUIAmmoMessage>(
		TAG_UI_Ammo, this,
		[this](FGameplayTag Tag, const FUIAmmoMessage& Msg) { HandleAmmo(Tag, Msg); });

	ReloadHandle = MsgSub->RegisterListener<FUIReloadMessage>(
		TAG_UI_Reload, this,
		[this](FGameplayTag Tag, const FUIReloadMessage& Msg) { HandleReload(Tag, Msg); });

	InteractionHandle = MsgSub->RegisterListener<FUIInteractionMessage>(
		TAG_UI_Interaction, this,
		[this](FGameplayTag Tag, const FUIInteractionMessage& Msg) { HandleInteraction(Tag, Msg); });
}

void UFlecsHUDWidget::NativeDestruct()
{
	HealthHandle.Unregister();
	DeathHandle.Unregister();
	AmmoHandle.Unregister();
	ReloadHandle.Unregister();
	InteractionHandle.Unregister();

	Super::NativeDestruct();
}

// ═══════════════════════════════════════════════════════════════
// PUBLIC API
// ═══════════════════════════════════════════════════════════════

void UFlecsHUDWidget::SetPlayerEntityId(int64 InEntityId)
{
	CachedPlayerEntityId = InEntityId;
}

void UFlecsHUDWidget::SetWeaponEntityId(int64 InWeaponEntityId)
{
	CachedWeaponEntityId = InWeaponEntityId;
}

// ═══════════════════════════════════════════════════════════════
// MESSAGE HANDLERS
// ═══════════════════════════════════════════════════════════════

void UFlecsHUDWidget::HandleHealth(FGameplayTag Channel, const FUIHealthMessage& Msg)
{
	if (CachedPlayerEntityId == 0 || Msg.EntityId != CachedPlayerEntityId) return;

	float Percent = Msg.MaxHP > 0.f ? Msg.CurrentHP / Msg.MaxHP : 0.f;
	OnHealthChanged(Msg.CurrentHP, Msg.MaxHP, Percent);
}

void UFlecsHUDWidget::HandleDeath(FGameplayTag Channel, const FUIDeathMessage& Msg)
{
	if (CachedPlayerEntityId == 0 || Msg.EntityId != CachedPlayerEntityId) return;

	OnPlayerDeath();
}

void UFlecsHUDWidget::HandleAmmo(FGameplayTag Channel, const FUIAmmoMessage& Msg)
{
	if (CachedWeaponEntityId == 0 || Msg.WeaponEntityId != CachedWeaponEntityId) return;

	OnAmmoChanged(Msg.CurrentAmmo, Msg.MagazineSize, Msg.ReserveAmmo);
}

void UFlecsHUDWidget::HandleReload(FGameplayTag Channel, const FUIReloadMessage& Msg)
{
	if (CachedWeaponEntityId == 0 || Msg.WeaponEntityId != CachedWeaponEntityId) return;

	if (Msg.bStarted)
	{
		OnReloadStarted();
	}
	else
	{
		OnReloadFinished(Msg.NewAmmo, Msg.MagazineSize);
	}
}

void UFlecsHUDWidget::HandleInteraction(FGameplayTag Channel, const FUIInteractionMessage& Msg)
{
	FText Prompt;
	if (Msg.bHasTarget)
	{
		Prompt = ResolveInteractionPrompt(Msg.TargetKey);
	}
	OnInteractionChanged(Msg.bHasTarget, Prompt);
}

// ═══════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════

FText UFlecsHUDWidget::ResolveInteractionPrompt(FSkeletonKey TargetKey) const
{
	UFlecsArtillerySubsystem* Sub = UFlecsArtillerySubsystem::SelfPtr;
	if (!Sub) return FText::GetEmpty();

	flecs::entity E = Sub->GetEntityForBarrageKey(TargetKey);
	if (!E.is_valid()) return FText::GetEmpty();

	const FEntityDefinitionRef* DefRef = E.try_get<FEntityDefinitionRef>();
	if (DefRef && DefRef->Definition && DefRef->Definition->InteractionProfile)
	{
		return DefRef->Definition->InteractionProfile->InteractionPrompt;
	}

	return NSLOCTEXT("Interaction", "Default", "Press E");
}
