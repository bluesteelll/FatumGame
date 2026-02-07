// UI message structs and GameplayTag channels for the message subsystem.
//
// All payloads are POD-like — no FText, FString, TArray.
// Complex data (prompts, names) is resolved on game thread from entity definitions.
//
// Usage (sim thread):
//   UFlecsMessageSubsystem::SelfPtr->EnqueueMessage(TAG_UI_Health, HealthMsg);
//
// Usage (game thread):
//   Subsystem->BroadcastMessage(TAG_UI_Health, HealthMsg);

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "SkeletonTypes.h"
#include "FlecsUIMessages.generated.h"

// ═══════════════════════════════════════════════════════════════
// GAMEPLAY TAG CHANNELS
// ═══════════════════════════════════════════════════════════════

UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_UI_Health);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_UI_Death);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_UI_Ammo);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_UI_Reload);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_UI_Interaction);

// ═══════════════════════════════════════════════════════════════
// MESSAGE STRUCTS
// ═══════════════════════════════════════════════════════════════

/** Health changed event. Fired by health observer on FHealthInstance modification. */
USTRUCT(BlueprintType)
struct FUIHealthMessage
{
	GENERATED_BODY()

	/** Flecs entity ID of the affected entity */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	int64 EntityId = 0;

	/** Current HP after change */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	float CurrentHP = 0.f;

	/** Max HP from prefab */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	float MaxHP = 0.f;
};

/** Entity death event. Fired by DeathCheckSystem when FTagDead is added. */
USTRUCT(BlueprintType)
struct FUIDeathMessage
{
	GENERATED_BODY()

	/** Flecs entity ID of the dead entity */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	int64 EntityId = 0;

	/** Flecs entity ID of the killer (0 = environment/unknown) */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	int64 KillerEntityId = 0;
};

/** Ammo state changed. Fired by WeaponFireSystem after shot. */
USTRUCT(BlueprintType)
struct FUIAmmoMessage
{
	GENERATED_BODY()

	/** Flecs entity ID of the weapon */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	int64 WeaponEntityId = 0;

	/** Current ammo in magazine */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	int32 CurrentAmmo = 0;

	/** Magazine capacity from prefab */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	int32 MagazineSize = 0;

	/** Reserve ammo count */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	int32 ReserveAmmo = 0;
};

/** Reload started/finished. Fired by WeaponReloadSystem. */
USTRUCT(BlueprintType)
struct FUIReloadMessage
{
	GENERATED_BODY()

	/** Flecs entity ID of the weapon */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	int64 WeaponEntityId = 0;

	/** true = reload started, false = reload finished */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	bool bStarted = true;

	/** New ammo count (valid only when bStarted == false) */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	int32 NewAmmo = 0;

	/** Magazine capacity from prefab */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	int32 MagazineSize = 0;
};

/**
 * Interaction target changed. Fired by interaction trace on game thread.
 * Prompt text is NOT in this message — read from EntityDefinition→InteractionProfile.
 */
USTRUCT(BlueprintType)
struct FUIInteractionMessage
{
	GENERATED_BODY()

	/** Flecs entity ID of the interaction target (0 = no target) */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	int64 EntityId = 0;

	/** BarrageKey of the target (for physics queries) */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	FSkeletonKey TargetKey;

	/** Whether there is a valid interaction target */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	bool bHasTarget = false;
};
