// Interaction state machine implementation for AFlecsCharacter.
// Handles Focus (camera + UI), Hold (progress bar), and Instant (pickup/toggle/destroy) interactions.

#include "FlecsCharacter.h"
#include "FlecsInteractionProfile.h"
#include "FlecsEntityDefinition.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsStaticComponents.h"
#include "FlecsInteractionLibrary.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"
#include "FlecsUIPanel.h"
#include "FlecsInventoryWidget.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Async/Async.h"

// =================================================================
// EASING FUNCTIONS
// =================================================================

static float EaseInOutCubic(float T)
{
	return T < 0.5f
		? 4.f * T * T * T
		: 1.f - FMath::Pow(-2.f * T + 2.f, 3.f) / 2.f;
}

static float EaseOutQuad(float T)
{
	return 1.f - (1.f - T) * (1.f - T);
}

// =================================================================
// QUERIES
// =================================================================

bool AFlecsCharacter::IsInInteraction() const
{
	return InteractionState != EInteractionState::Gameplay;
}

void AFlecsCharacter::SetInteractionState(EInteractionState NewState)
{
	if (InteractionState == NewState) return;
	InteractionState = NewState;
	OnInteractionStateChanged(static_cast<uint8>(NewState));

	if (UFlecsMessageSubsystem* MsgSub = UFlecsMessageSubsystem::Get(this))
	{
		FUIInteractionStateMessage Msg;
		Msg.State = static_cast<uint8>(NewState);
		Msg.TargetKey = ActiveInteractionTargetKey;
		MsgSub->BroadcastMessage(TAG_UI_InteractionState, Msg);
	}
}

const UFlecsInteractionProfile* AFlecsCharacter::ResolveInteractionProfile(FSkeletonKey TargetKey) const
{
	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!FlecsSubsystem) return nullptr;

	flecs::entity E = FlecsSubsystem->GetEntityForBarrageKey(TargetKey);
	if (!E.is_valid()) return nullptr;

	const FEntityDefinitionRef* DefRef = E.try_get<FEntityDefinitionRef>();
	if (DefRef && DefRef->Definition)
	{
		return DefRef->Definition->InteractionProfile;
	}
	return nullptr;
}

bool AFlecsCharacter::GetEntityWorldPosition(FSkeletonKey EntityKey, FVector& OutPosition) const
{
	UBarrageDispatch* Barrage = GetWorld()->GetSubsystem<UBarrageDispatch>();
	if (!Barrage) return false;

	FBLet Body = Barrage->GetShapeRef(EntityKey);
	if (FBarragePrimitive::IsNotNull(Body))
	{
		FVector3f Pos = FBarragePrimitive::GetPosition(Body);
		OutPosition = FVector(Pos);
		return true;
	}
	return false;
}

bool AFlecsCharacter::GetEntityWorldTransform(FSkeletonKey EntityKey, FVector& OutPosition, FQuat& OutRotation) const
{
	UBarrageDispatch* Barrage = GetWorld()->GetSubsystem<UBarrageDispatch>();
	if (!Barrage) return false;

	FBLet Body = Barrage->GetShapeRef(EntityKey);
	if (FBarragePrimitive::IsNotNull(Body))
	{
		OutPosition = FVector(FBarragePrimitive::GetPosition(Body));
		OutRotation = FQuat(FBarragePrimitive::OptimisticGetAbsoluteRotation(Body));
		return true;
	}
	return false;
}

// =================================================================
// INPUT ROUTING
// =================================================================

void AFlecsCharacter::HandleInteractionInput()
{
	// If already in a state, handle context-specific E press
	switch (InteractionState)
	{
	case EInteractionState::Gameplay:
		break; // Fall through to new interaction below
	case EInteractionState::Focusing:
		return; // Ignore during transition
	case EInteractionState::Focused:
		BeginUnfocusTransition(); // E exits focus
		return;
	case EInteractionState::Unfocusing:
		return; // Ignore during transition
	case EInteractionState::Holding:
		return; // Hold is driven by key held state, not presses
	}

	// Start new interaction
	if (!CurrentInteractionTarget.IsValid()) return;

	const UFlecsInteractionProfile* Profile = ResolveInteractionProfile(CurrentInteractionTarget);
	if (!Profile)
	{
		// Backward compatibility: entities with tags but no InteractionProfile
		FOnContainerOpened LegacyCallback;
		LegacyCallback.BindWeakLambda(this, [this](int64 ContainerId, const FText& Title)
		{
			OpenLootPanel(ContainerId, Title);
		});
		UFlecsInteractionLibrary::DispatchLegacyInteraction(
			this, CurrentInteractionTarget, InventoryEntityId,
			CachedInteractionPrompt, LegacyCallback);
		return;
	}

	ActiveInteractionProfile = Profile;
	ActiveInteractionTargetKey = CurrentInteractionTarget;

	switch (Profile->InteractionType)
	{
	case EInteractionType::Instant:
	{
		FOnContainerOpened ContainerCallback;
		ContainerCallback.BindWeakLambda(this, [this](int64 ContainerId, const FText& Title)
		{
			OpenLootPanel(ContainerId, Title);
		});
		UFlecsInteractionLibrary::DispatchInstantAction(
			this, Profile->InstantAction, CurrentInteractionTarget,
			InventoryEntityId, Profile->CustomEventTag,
			CachedInteractionPrompt, ContainerCallback);
		UFlecsInteractionLibrary::ApplySingleUseIfNeeded(this, CurrentInteractionTarget);
		break;
	}

	case EInteractionType::Focus:
		BeginFocusTransition();
		break;

	case EInteractionType::Hold:
		BeginHoldInteraction();
		break;
	}
}

void AFlecsCharacter::HandleInteractionRelease()
{
	bInteractKeyHeld = false;
	// Hold cancellation is handled in TickInteractionStateMachine via bInteractKeyHeld
}

void AFlecsCharacter::HandleInteractionCancel()
{
	switch (InteractionState)
	{
	case EInteractionState::Focused:
		BeginUnfocusTransition();
		return;

	case EInteractionState::Holding:
		if (bHoldCanCancel) CancelHoldInteraction();
		return;

	case EInteractionState::Gameplay:
		// Priority: loot first, then standalone inventory.
		if (IsLootOpen())
		{
			CloseLootPanel();
			return;
		}
		if (IsInventoryOpen())
		{
			InventoryWidget->CloseInventory();
			InventoryWidget->SetVisibility(ESlateVisibility::Collapsed);
			return;
		}
		break;

	default:
		// Focusing / Unfocusing: ignore during camera transitions.
		break;
	}
}

// =================================================================
// TICK
// =================================================================

void AFlecsCharacter::TickInteractionStateMachine(float DeltaTime)
{
	switch (InteractionState)
	{
	case EInteractionState::Gameplay:
		return;

	case EInteractionState::Focusing:
	{
		checkf(CurrentTransitionDuration > 0.f, TEXT("TickInteraction: TransitionDuration must be > 0"));
		FocusLerpAlpha += DeltaTime / CurrentTransitionDuration;

		if (FocusLerpAlpha >= 1.f)
		{
			FocusLerpAlpha = 1.f;
			ApplyFocusCameraLerp(1.f);
			SetInteractionState(EInteractionState::Focused);

			// Open UI panel if configured
			OpenFocusPanel();
		}
		else
		{
			ApplyFocusCameraLerp(EaseInOutCubic(FocusLerpAlpha));
		}
		break;
	}

	case EInteractionState::Focused:
		// Waiting for player input (E/Escape to exit)
		break;

	case EInteractionState::Unfocusing:
	{
		checkf(CurrentTransitionDuration > 0.f, TEXT("TickInteraction: TransitionDuration must be > 0"));
		FocusLerpAlpha -= DeltaTime / CurrentTransitionDuration;

		if (FocusLerpAlpha <= 0.f)
		{
			FocusLerpAlpha = 0.f;
			ApplyFocusCameraLerp(0.f);
			RestoreCameraControl();
			SetInteractionState(EInteractionState::Gameplay);
			ActiveInteractionProfile = nullptr;
			ActiveInteractionTargetKey = FSkeletonKey();
		}
		else
		{
			ApplyFocusCameraLerp(EaseOutQuad(FocusLerpAlpha));
		}
		break;
	}

	case EInteractionState::Holding:
	{
		// Cancel if E released
		if (!bInteractKeyHeld && bHoldCanCancel)
		{
			CancelHoldInteraction();
			return;
		}

		// Cancel if target lost (with grace period to handle 10Hz trace flicker)
		if (!CurrentInteractionTarget.IsValid() ||
			CurrentInteractionTarget != ActiveInteractionTargetKey)
		{
			HoldTargetLostTime += DeltaTime;
			if (HoldTargetLostTime >= 0.3f)
			{
				CancelHoldInteraction();
				return;
			}
		}
		else
		{
			HoldTargetLostTime = 0.f;
		}

		HoldAccumulator += DeltaTime;
		float Progress = FMath::Clamp(HoldAccumulator / HoldRequiredDuration, 0.f, 1.f);

		// Broadcast progress
		OnHoldProgressChanged(Progress);
		if (UFlecsMessageSubsystem* MsgSub = UFlecsMessageSubsystem::Get(this))
		{
			FUIHoldProgressMessage Msg;
			Msg.Progress = Progress;
			Msg.TotalDuration = HoldRequiredDuration;
			MsgSub->BroadcastMessage(TAG_UI_HoldProgress, Msg);
		}

		if (HoldAccumulator >= HoldRequiredDuration)
		{
			CompleteHoldInteraction();
		}
		break;
	}
	}
}

// =================================================================
// FOCUS CAMERA
// =================================================================

FTransform AFlecsCharacter::ComputeFocusCameraTransform(
	FVector EntityPos, FQuat EntityRot,
	FVector LocalCameraPos, FRotator LocalCameraRot) const
{
	// Camera position: entity origin + local offset rotated by entity orientation
	FVector FinalCameraPos = EntityPos + EntityRot.RotateVector(LocalCameraPos);

	// Camera rotation: local rotation composed with entity orientation
	FQuat FinalCameraRot = EntityRot * LocalCameraRot.Quaternion();

	return FTransform(FinalCameraRot, FinalCameraPos);
}

void AFlecsCharacter::ApplyFocusCameraLerp(float Alpha)
{
	check(FollowCamera);
	FVector LerpedPos = FMath::Lerp(SavedCameraTransform.GetLocation(), FocusCameraTarget.GetLocation(), Alpha);
	FQuat LerpedRot = FQuat::Slerp(SavedCameraTransform.GetRotation(), FocusCameraTarget.GetRotation(), Alpha);
	FollowCamera->SetWorldLocationAndRotation(LerpedPos, LerpedRot.Rotator());

	if (FocusTargetFOV > 0.f)
	{
		float LerpedFOV = FMath::Lerp(SavedCameraFOV, FocusTargetFOV, Alpha);
		FollowCamera->SetFieldOfView(LerpedFOV);
	}
}

void AFlecsCharacter::BeginFocusTransition()
{
	check(ActiveInteractionProfile);
	check(ActiveInteractionProfile->InteractionType == EInteractionType::Focus);
	check(FollowCamera);

	// For Focus without camera movement, skip directly to Focused
	if (!ActiveInteractionProfile->bMoveCamera)
	{
		// Block movement and input
		APlayerController* PC = Cast<APlayerController>(Controller);
		check(PC);
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);

		// Swap input contexts
		if (auto* InputSub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			FModifyContextOptions Opts;
			Opts.bIgnoreAllPressedKeysUntilRelease = true;
			if (GameplayMappingContext) InputSub->RemoveMappingContext(GameplayMappingContext, Opts);
			if (InventoryMappingContext) InputSub->AddMappingContext(InventoryMappingContext, 1, Opts);
		}

		PC->SetShowMouseCursor(true);
		SetInteractionState(EInteractionState::Focused);
		OpenFocusPanel();
		return;
	}

	// Cache entity transform at start (don't track moving entity)
	FVector EntityPos;
	FQuat EntityRot;
	if (!GetEntityWorldTransform(ActiveInteractionTargetKey, EntityPos, EntityRot))
	{
		UE_LOG(LogTemp, Warning, TEXT("BeginFocusTransition: Entity has no valid position, aborting"));
		ActiveInteractionProfile = nullptr;
		ActiveInteractionTargetKey = FSkeletonKey();
		return;
	}

	// Resolve camera position/rotation: per-instance override > InteractionProfile default
	FVector LocalCamPos = ActiveInteractionProfile->FocusCameraPosition;
	FRotator LocalCamRot = ActiveInteractionProfile->FocusCameraRotation;

	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (ensure(FlecsSubsystem))
	{
		flecs::entity E = FlecsSubsystem->GetEntityForBarrageKey(ActiveInteractionTargetKey);
		if (E.is_valid())
		{
			const FFocusCameraOverride* Override = E.try_get<FFocusCameraOverride>();
			if (Override)
			{
				LocalCamPos = Override->CameraPosition;
				LocalCamRot = Override->CameraRotation;
			}
		}
	}

	// Compute target camera transform
	FocusCameraTarget = ComputeFocusCameraTransform(EntityPos, EntityRot, LocalCamPos, LocalCamRot);
	FocusTargetFOV = ActiveInteractionProfile->FocusFOV;
	CurrentTransitionDuration = ActiveInteractionProfile->TransitionInTime;
	FocusLerpAlpha = 0.f;

	// Save current camera state
	SavedCameraTransform = FollowCamera->GetComponentTransform();
	SavedCameraFOV = FollowCamera->FieldOfView;

	// Disable camera following — we drive it manually via lerp
	FollowCamera->bUsePawnControlRotation = false;

	// Block movement and input
	APlayerController* PC = Cast<APlayerController>(Controller);
	check(PC);
	PC->SetIgnoreMoveInput(true);
	PC->SetIgnoreLookInput(true);

	// Swap input contexts
	if (auto* InputSub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		FModifyContextOptions Opts;
		Opts.bIgnoreAllPressedKeysUntilRelease = true;
		if (GameplayMappingContext) InputSub->RemoveMappingContext(GameplayMappingContext, Opts);
		if (InventoryMappingContext) InputSub->AddMappingContext(InventoryMappingContext, 1, Opts);
	}

	PC->SetShowMouseCursor(true);
	SetInteractionState(EInteractionState::Focusing);
}

void AFlecsCharacter::BeginUnfocusTransition(float OverrideDuration)
{
	// Close panel first
	CloseFocusPanel();

	// Apply single-use after focus session
	if (ActiveInteractionTargetKey.IsValid())
	{
		UFlecsInteractionLibrary::ApplySingleUseIfNeeded(this, ActiveInteractionTargetKey);
	}

	// For Focus without camera movement, skip directly to Gameplay
	if (ActiveInteractionProfile && !ActiveInteractionProfile->bMoveCamera)
	{
		RestoreCameraControl();
		SetInteractionState(EInteractionState::Gameplay);
		ActiveInteractionProfile = nullptr;
		ActiveInteractionTargetKey = FSkeletonKey();
		return;
	}

	float Duration = OverrideDuration > 0.f ? OverrideDuration
		: (ActiveInteractionProfile ? ActiveInteractionProfile->TransitionOutTime : 0.25f);
	CurrentTransitionDuration = Duration;

	SetInteractionState(EInteractionState::Unfocusing);
}

void AFlecsCharacter::RestoreCameraControl()
{
	check(FollowCamera);

	// Restore camera rotation control (first-person mode)
	FollowCamera->bUsePawnControlRotation = true;
	FollowCamera->SetFieldOfView(SavedCameraFOV);

	// Restore player input
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (PC)
	{
		PC->ResetIgnoreLookInput();
		PC->ResetIgnoreMoveInput();
		PC->SetShowMouseCursor(false);

		// Restore input contexts
		if (auto* InputSub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			FModifyContextOptions Opts;
			Opts.bIgnoreAllPressedKeysUntilRelease = true;
			if (InventoryMappingContext) InputSub->RemoveMappingContext(InventoryMappingContext, Opts);
			if (GameplayMappingContext) InputSub->AddMappingContext(GameplayMappingContext, 0, Opts);
		}
	}
}

// =================================================================
// FOCUS PANEL
// =================================================================

void AFlecsCharacter::OpenFocusPanel()
{
	if (!ActiveInteractionProfile || !ActiveInteractionProfile->FocusWidgetClass) return;

	APlayerController* PC = Cast<APlayerController>(Controller);
	check(PC);

	ActiveFocusPanel = CreateWidget<UFlecsUIPanel>(PC, ActiveInteractionProfile->FocusWidgetClass);
	if (!ActiveFocusPanel)
	{
		UE_LOG(LogTemp, Warning, TEXT("OpenFocusPanel: Failed to create widget of class %s"),
			*ActiveInteractionProfile->FocusWidgetClass->GetName());
		return;
	}

	ActiveFocusPanel->GameplayMappingContext = GameplayMappingContext;
	ActiveFocusPanel->PanelMappingContext = InventoryMappingContext;
	ActiveFocusPanel->AddToViewport(10);
	ActiveFocusPanel->ActivateWidget();
}

void AFlecsCharacter::CloseFocusPanel()
{
	if (ActiveFocusPanel)
	{
		ActiveFocusPanel->DeactivateWidget();
		ActiveFocusPanel->RemoveFromParent();
		ActiveFocusPanel = nullptr;
	}
}

// =================================================================
// HOLD INTERACTION
// =================================================================

void AFlecsCharacter::BeginHoldInteraction()
{
	check(ActiveInteractionProfile);
	check(ActiveInteractionProfile->InteractionType == EInteractionType::Hold);

	HoldAccumulator = 0.f;
	HoldRequiredDuration = ActiveInteractionProfile->HoldDuration;
	HoldTargetLostTime = 0.f;
	bHoldCanCancel = ActiveInteractionProfile->bCanCancel;
	bInteractKeyHeld = true;

	SetInteractionState(EInteractionState::Holding);
}

void AFlecsCharacter::CancelHoldInteraction()
{
	// Broadcast cancellation
	if (UFlecsMessageSubsystem* MsgSub = UFlecsMessageSubsystem::Get(this))
	{
		FUIHoldProgressMessage Msg;
		Msg.Progress = FMath::Clamp(HoldAccumulator / HoldRequiredDuration, 0.f, 1.f);
		Msg.TotalDuration = HoldRequiredDuration;
		Msg.bFinished = true;
		Msg.bCompleted = false;
		MsgSub->BroadcastMessage(TAG_UI_HoldProgress, Msg);
	}

	OnHoldProgressChanged(0.f);
	SetInteractionState(EInteractionState::Gameplay);
	ActiveInteractionProfile = nullptr;
	ActiveInteractionTargetKey = FSkeletonKey();
	HoldAccumulator = 0.f;
	bInteractKeyHeld = false;
}

void AFlecsCharacter::CompleteHoldInteraction()
{
	// Broadcast completion
	if (UFlecsMessageSubsystem* MsgSub = UFlecsMessageSubsystem::Get(this))
	{
		FUIHoldProgressMessage Msg;
		Msg.Progress = 1.f;
		Msg.TotalDuration = HoldRequiredDuration;
		Msg.bFinished = true;
		Msg.bCompleted = true;
		MsgSub->BroadcastMessage(TAG_UI_HoldProgress, Msg);
	}

	// Execute completion action
	check(ActiveInteractionProfile);
	FOnContainerOpened ContainerCallback;
	ContainerCallback.BindWeakLambda(this, [this](int64 ContainerId, const FText& Title)
	{
		OpenLootPanel(ContainerId, Title);
	});
	UFlecsInteractionLibrary::DispatchInstantAction(
		this, ActiveInteractionProfile->CompletionAction, ActiveInteractionTargetKey,
		InventoryEntityId, ActiveInteractionProfile->HoldCompletionEventTag,
		CachedInteractionPrompt, ContainerCallback);
	UFlecsInteractionLibrary::ApplySingleUseIfNeeded(this, ActiveInteractionTargetKey);

	SetInteractionState(EInteractionState::Gameplay);
	ActiveInteractionProfile = nullptr;
	ActiveInteractionTargetKey = FSkeletonKey();
	HoldAccumulator = 0.f;
	bInteractKeyHeld = false;
}

void AFlecsCharacter::ForceCancelInteraction()
{
	switch (InteractionState)
	{
	case EInteractionState::Gameplay:
		return;

	case EInteractionState::Focusing:
	case EInteractionState::Focused:
	{
		if (ActiveInteractionProfile && !ActiveInteractionProfile->bAllowDamageCancel)
			return;

		CloseFocusPanel();

		// For no-camera focus, just restore directly
		if (ActiveInteractionProfile && !ActiveInteractionProfile->bMoveCamera)
		{
			RestoreCameraControl();
			SetInteractionState(EInteractionState::Gameplay);
			ActiveInteractionProfile = nullptr;
			ActiveInteractionTargetKey = FSkeletonKey();
			return;
		}

		// Fast camera exit
		float FastDuration = ActiveInteractionProfile
			? ActiveInteractionProfile->DamageCancelTransitionTime : 0.15f;
		CurrentTransitionDuration = FastDuration;
		SetInteractionState(EInteractionState::Unfocusing);
		break;
	}

	case EInteractionState::Unfocusing:
		return; // Already exiting

	case EInteractionState::Holding:
	{
		if (ActiveInteractionProfile && !ActiveInteractionProfile->bAllowDamageCancel)
			return;
		CancelHoldInteraction();
		break;
	}
	}
}

// Dispatch functions moved to UFlecsInteractionLibrary (FlecsInteractionLibrary.h/cpp).
// Character now calls Library static methods with FOnContainerOpened callback for UI.
