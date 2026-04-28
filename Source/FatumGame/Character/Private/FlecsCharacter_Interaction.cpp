// Interaction implementation for AFlecsCharacter.
// Detection (10Hz Barrage raycast), state machine (Focus/Hold/Instant), camera transitions.

#include "FlecsCharacter.h"
#include "FlecsInteractionProfile.h"
#include "FlecsEntityDefinition.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsEntityComponents.h"
#include "FlecsInteractionComponents.h"
#include "FlecsGameTags.h"
#include "FlecsCraftingComponents.h"
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

// Phase 4 — wrench-aware interaction routing.
#include "Components/FlecsMultiblockComponents.h"
#include "Library/FlecsCraftingLibrary.h"
#include "Library/FlecsMultiblockRuntime.h"
#include "FlecsMultiblockBlueprint.h"

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
	return Interact.State != EInteractionState::Gameplay;
}

void AFlecsCharacter::SetInteractionState(EInteractionState NewState)
{
	if (Interact.State == NewState) return;
	Interact.State = NewState;
	OnInteractionStateChanged(static_cast<uint8>(NewState));

	if (UFlecsMessageSubsystem* MsgSub = UFlecsMessageSubsystem::Get(this))
	{
		FUIInteractionStateMessage Msg;
		Msg.State = static_cast<uint8>(NewState);
		Msg.TargetKey = Interact.ActiveTargetKey;
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
// PHASE 4 — WRENCH SUPPORT
// =================================================================

bool AFlecsCharacter::IsWrenchEquipped() const
{
	if (ActiveWeaponEntityId == 0) return false;
	UFlecsArtillerySubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>() : nullptr;
	if (!Sub) return false;
	flecs::world* W = Sub->GetFlecsWorld();
	if (!W) return false;
	flecs::entity E = W->entity(static_cast<flecs::entity_t>(ActiveWeaponEntityId));
	return E.is_valid() && E.is_alive() && E.has<FTagWrench>();
}

// =================================================================
// INPUT ROUTING
// =================================================================

void AFlecsCharacter::HandleInteractionInput()
{
	// If already in a state, handle context-specific E press
	switch (Interact.State)
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

	// ─── Phase 4 — wrench branch takes priority over standard routing ───
	if (IsWrenchEquipped())
	{
		switch (Interact.WrenchHoverKind)
		{
		case EWrenchHoverKind::SwappableFunctional:
		{
			const FSkeletonKey ChildKey(static_cast<uint64>(Interact.WrenchHoverTargetId));
			UFlecsCraftingLibrary::RequestPartDetach(this, ChildKey);
			return;
		}
		case EWrenchHoverKind::EmptyExtensionPort:
		{
			const FSkeletonKey StationKey(static_cast<uint64>(Interact.WrenchHoverTargetId));
			UFlecsCraftingLibrary::RequestPartAttachAuto(
				this, StationKey, Interact.WrenchHoverPortIndex, InventoryEntityId);
			return;
		}
		case EWrenchHoverKind::Anchor:
		{
			// Hold-E for full deconstruct.
			Interact.bHoldIsWrenchDeconstruct = true;
			Interact.HoldAccumulator = 0.f;
			Interact.HoldRequiredDuration = 1.f;
			Interact.HoldTargetLostTime = 0.f;
			Interact.bHoldCanCancel = true;
			Interact.bInteractKeyHeld = true;
			Interact.ActiveTargetKey = FSkeletonKey(static_cast<uint64>(Interact.WrenchHoverTargetId));
			SetInteractionState(EInteractionState::Holding);
			return;
		}
		case EWrenchHoverKind::RigidFunctional:
		case EWrenchHoverKind::OccupiedExtensionPort:
		case EWrenchHoverKind::NotApplicable:
		default:
			// No-op — designer feedback only (red glow / "rigid" tooltip).
			return;
		}
	}

	// Start new interaction (legacy non-wrench path).
	if (!Interact.CurrentTarget.IsValid()) return;

	const UFlecsInteractionProfile* Profile = ResolveInteractionProfile(Interact.CurrentTarget);
	if (!Profile)
	{
		// Backward compatibility: entities with tags but no InteractionProfile
		FOnContainerOpened LegacyCallback;
		LegacyCallback.BindWeakLambda(this, [this](int64 ContainerId, const FText& Title)
		{
			OpenLootPanel(ContainerId, Title);
		});
		UFlecsInteractionLibrary::DispatchLegacyInteraction(
			this, Interact.CurrentTarget, InventoryEntityId,
			Interact.CachedPrompt, LegacyCallback);
		return;
	}

	Interact.ActiveProfile = Profile;
	Interact.ActiveTargetKey = Interact.CurrentTarget;

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
			this, Profile->InstantAction, Interact.CurrentTarget,
			InventoryEntityId, Profile->CustomEventTag,
			Interact.CachedPrompt, ContainerCallback);
		UFlecsInteractionLibrary::ApplySingleUseIfNeeded(this, Interact.CurrentTarget);
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
	Interact.bInteractKeyHeld = false;
	// Hold cancellation is handled in TickInteractionStateMachine via bInteractKeyHeld
}

void AFlecsCharacter::HandleInteractionCancel()
{
	switch (Interact.State)
	{
	case EInteractionState::Focused:
		BeginUnfocusTransition();
		return;

	case EInteractionState::Holding:
		if (Interact.bHoldCanCancel) CancelHoldInteraction();
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
	switch (Interact.State)
	{
	case EInteractionState::Gameplay:
		return;

	case EInteractionState::Focusing:
	{
		checkf(Interact.CurrentTransitionDuration > 0.f, TEXT("TickInteraction: TransitionDuration must be > 0"));
		Interact.FocusLerpAlpha += DeltaTime / Interact.CurrentTransitionDuration;

		if (Interact.FocusLerpAlpha >= 1.f)
		{
			Interact.FocusLerpAlpha = 1.f;
			ApplyFocusCameraLerp(1.f);
			SetInteractionState(EInteractionState::Focused);

			// Open UI panel if configured
			OpenFocusPanel();
		}
		else
		{
			ApplyFocusCameraLerp(EaseInOutCubic(Interact.FocusLerpAlpha));
		}
		break;
	}

	case EInteractionState::Focused:
		// Waiting for player input (E/Escape to exit)
		break;

	case EInteractionState::Unfocusing:
	{
		checkf(Interact.CurrentTransitionDuration > 0.f, TEXT("TickInteraction: TransitionDuration must be > 0"));
		Interact.FocusLerpAlpha -= DeltaTime / Interact.CurrentTransitionDuration;

		if (Interact.FocusLerpAlpha <= 0.f)
		{
			Interact.FocusLerpAlpha = 0.f;
			ApplyFocusCameraLerp(0.f);
			RestoreCameraControl();
			SetInteractionState(EInteractionState::Gameplay);
			Interact.ActiveProfile = nullptr;
			Interact.ActiveTargetKey = FSkeletonKey();
		}
		else
		{
			ApplyFocusCameraLerp(EaseOutQuad(Interact.FocusLerpAlpha));
		}
		break;
	}

	case EInteractionState::Holding:
	{
		// Cancel if E released
		if (!Interact.bInteractKeyHeld && Interact.bHoldCanCancel)
		{
			CancelHoldInteraction();
			return;
		}

		// Cancel if target lost (with grace period to handle 10Hz trace flicker).
		// Phase 4 — for wrench-deconstruct sub-state, target identity check uses
		// WrenchHoverKind == Anchor + WrenchHoverTargetId match (Flecs id, not BarrageKey).
		bool bTargetLost = false;
		if (Interact.bHoldIsWrenchDeconstruct)
		{
			const int64 ExpectedId = static_cast<int64>(Interact.ActiveTargetKey.Obj);
			if (Interact.WrenchHoverKind != EWrenchHoverKind::Anchor
				|| Interact.WrenchHoverTargetId != ExpectedId)
			{
				bTargetLost = true;
			}
		}
		else if (!Interact.CurrentTarget.IsValid() ||
			Interact.CurrentTarget != Interact.ActiveTargetKey)
		{
			bTargetLost = true;
		}

		if (bTargetLost)
		{
			Interact.HoldTargetLostTime += DeltaTime;
			if (Interact.HoldTargetLostTime >= 0.3f)
			{
				CancelHoldInteraction();
				return;
			}
		}
		else
		{
			Interact.HoldTargetLostTime = 0.f;
		}

		Interact.HoldAccumulator += DeltaTime;
		float Progress = FMath::Clamp(Interact.HoldAccumulator / Interact.HoldRequiredDuration, 0.f, 1.f);

		// Broadcast progress
		OnHoldProgressChanged(Progress);
		if (UFlecsMessageSubsystem* MsgSub = UFlecsMessageSubsystem::Get(this))
		{
			FUIHoldProgressMessage Msg;
			Msg.Progress = Progress;
			Msg.TotalDuration = Interact.HoldRequiredDuration;
			MsgSub->BroadcastMessage(TAG_UI_HoldProgress, Msg);
		}

		if (Interact.HoldAccumulator >= Interact.HoldRequiredDuration)
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
	FVector FinalCameraPos = EntityPos + EntityRot.RotateVector(LocalCameraPos);
	FQuat FinalCameraRot = EntityRot * LocalCameraRot.Quaternion();
	return FTransform(FinalCameraRot, FinalCameraPos);
}

void AFlecsCharacter::ApplyFocusCameraLerp(float Alpha)
{
	check(FollowCamera);
	FVector LerpedPos = FMath::Lerp(Interact.SavedCameraTransform.GetLocation(), Interact.FocusCameraTarget.GetLocation(), Alpha);
	FQuat LerpedRot = FQuat::Slerp(Interact.SavedCameraTransform.GetRotation(), Interact.FocusCameraTarget.GetRotation(), Alpha);
	FollowCamera->SetWorldLocationAndRotation(LerpedPos, LerpedRot.Rotator());

	if (Interact.FocusTargetFOV > 0.f)
	{
		float LerpedFOV = FMath::Lerp(Interact.SavedCameraFOV, Interact.FocusTargetFOV, Alpha);
		FollowCamera->SetFieldOfView(LerpedFOV);
	}
}

void AFlecsCharacter::BeginFocusTransition()
{
	check(Interact.ActiveProfile);
	check(Interact.ActiveProfile->InteractionType == EInteractionType::Focus);
	check(FollowCamera);

	// For Focus without camera movement, skip directly to Focused
	if (!Interact.ActiveProfile->bMoveCamera)
	{
		APlayerController* PC = Cast<APlayerController>(Controller);
		check(PC);
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);

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
	if (!GetEntityWorldTransform(Interact.ActiveTargetKey, EntityPos, EntityRot))
	{
		UE_LOG(LogTemp, Warning, TEXT("BeginFocusTransition: Entity has no valid position, aborting"));
		Interact.ActiveProfile = nullptr;
		Interact.ActiveTargetKey = FSkeletonKey();
		return;
	}

	// Resolve camera position/rotation: per-instance override > InteractionProfile default
	FVector LocalCamPos = Interact.ActiveProfile->FocusCameraPosition;
	FRotator LocalCamRot = Interact.ActiveProfile->FocusCameraRotation;

	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (ensure(FlecsSubsystem))
	{
		flecs::entity E = FlecsSubsystem->GetEntityForBarrageKey(Interact.ActiveTargetKey);
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
	Interact.FocusCameraTarget = ComputeFocusCameraTransform(EntityPos, EntityRot, LocalCamPos, LocalCamRot);
	Interact.FocusTargetFOV = Interact.ActiveProfile->FocusFOV;
	Interact.CurrentTransitionDuration = Interact.ActiveProfile->TransitionInTime;
	Interact.FocusLerpAlpha = 0.f;

	// Save current camera state
	Interact.SavedCameraTransform = FollowCamera->GetComponentTransform();
	Interact.SavedCameraFOV = FollowCamera->FieldOfView;

	// Disable camera following — we drive it manually via lerp.
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
	if (Interact.ActiveTargetKey.IsValid())
	{
		UFlecsInteractionLibrary::ApplySingleUseIfNeeded(this, Interact.ActiveTargetKey);
	}

	// For Focus without camera movement, skip directly to Gameplay
	if (Interact.ActiveProfile && !Interact.ActiveProfile->bMoveCamera)
	{
		RestoreCameraControl();
		SetInteractionState(EInteractionState::Gameplay);
		Interact.ActiveProfile = nullptr;
		Interact.ActiveTargetKey = FSkeletonKey();
		return;
	}

	float Duration = OverrideDuration > 0.f ? OverrideDuration
		: (Interact.ActiveProfile ? Interact.ActiveProfile->TransitionOutTime : 0.25f);
	Interact.CurrentTransitionDuration = Duration;

	SetInteractionState(EInteractionState::Unfocusing);
}

void AFlecsCharacter::RestoreCameraControl()
{
	check(FollowCamera);

	FollowCamera->bUsePawnControlRotation = !bFirstPersonCamera;
	FollowCamera->SetFieldOfView(Interact.SavedCameraFOV);

	APlayerController* PC = Cast<APlayerController>(Controller);
	if (PC)
	{
		PC->ResetIgnoreLookInput();
		PC->ResetIgnoreMoveInput();
		PC->SetShowMouseCursor(false);

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
	if (!Interact.ActiveProfile || !Interact.ActiveProfile->FocusWidgetClass) return;

	APlayerController* PC = Cast<APlayerController>(Controller);
	check(PC);

	ActiveFocusPanel = CreateWidget<UFlecsUIPanel>(PC, Interact.ActiveProfile->FocusWidgetClass);
	if (!ActiveFocusPanel)
	{
		UE_LOG(LogTemp, Warning, TEXT("OpenFocusPanel: Failed to create widget of class %s"),
			*Interact.ActiveProfile->FocusWidgetClass->GetName());
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
	check(Interact.ActiveProfile);
	check(Interact.ActiveProfile->InteractionType == EInteractionType::Hold);

	Interact.HoldAccumulator = 0.f;
	Interact.HoldRequiredDuration = Interact.ActiveProfile->HoldDuration;
	Interact.HoldTargetLostTime = 0.f;
	Interact.bHoldCanCancel = Interact.ActiveProfile->bCanCancel;
	Interact.bInteractKeyHeld = true;

	SetInteractionState(EInteractionState::Holding);
}

void AFlecsCharacter::CancelHoldInteraction()
{
	if (UFlecsMessageSubsystem* MsgSub = UFlecsMessageSubsystem::Get(this))
	{
		FUIHoldProgressMessage Msg;
		Msg.Progress = FMath::Clamp(Interact.HoldAccumulator / Interact.HoldRequiredDuration, 0.f, 1.f);
		Msg.TotalDuration = Interact.HoldRequiredDuration;
		Msg.bFinished = true;
		Msg.bCompleted = false;
		MsgSub->BroadcastMessage(TAG_UI_HoldProgress, Msg);
	}

	OnHoldProgressChanged(0.f);
	SetInteractionState(EInteractionState::Gameplay);
	Interact.ActiveProfile = nullptr;
	Interact.ActiveTargetKey = FSkeletonKey();
	Interact.HoldAccumulator = 0.f;
	Interact.bInteractKeyHeld = false;
	Interact.bHoldIsWrenchDeconstruct = false;  // Phase 4 — clear sub-state
}

void AFlecsCharacter::CompleteHoldInteraction()
{
	if (UFlecsMessageSubsystem* MsgSub = UFlecsMessageSubsystem::Get(this))
	{
		FUIHoldProgressMessage Msg;
		Msg.Progress = 1.f;
		Msg.TotalDuration = Interact.HoldRequiredDuration;
		Msg.bFinished = true;
		Msg.bCompleted = true;
		MsgSub->BroadcastMessage(TAG_UI_HoldProgress, Msg);
	}

	// Phase 4 — wrench-Hold-E deconstruct path. ActiveProfile is null in this path
	// (we never resolved a profile because the wrench branch in HandleInteractionInput
	// goes directly to Holding without the legacy profile resolution).
	if (Interact.bHoldIsWrenchDeconstruct)
	{
		UFlecsCraftingLibrary::RequestStationDeconstruct(this, Interact.ActiveTargetKey);
		Interact.bHoldIsWrenchDeconstruct = false;
		SetInteractionState(EInteractionState::Gameplay);
		Interact.ActiveProfile = nullptr;
		Interact.ActiveTargetKey = FSkeletonKey();
		Interact.HoldAccumulator = 0.f;
		Interact.bInteractKeyHeld = false;
		return;
	}

	check(Interact.ActiveProfile);
	FOnContainerOpened ContainerCallback;
	ContainerCallback.BindWeakLambda(this, [this](int64 ContainerId, const FText& Title)
	{
		OpenLootPanel(ContainerId, Title);
	});
	UFlecsInteractionLibrary::DispatchInstantAction(
		this, Interact.ActiveProfile->CompletionAction, Interact.ActiveTargetKey,
		InventoryEntityId, Interact.ActiveProfile->HoldCompletionEventTag,
		Interact.CachedPrompt, ContainerCallback);
	UFlecsInteractionLibrary::ApplySingleUseIfNeeded(this, Interact.ActiveTargetKey);

	SetInteractionState(EInteractionState::Gameplay);
	Interact.ActiveProfile = nullptr;
	Interact.ActiveTargetKey = FSkeletonKey();
	Interact.HoldAccumulator = 0.f;
	Interact.bInteractKeyHeld = false;
}

void AFlecsCharacter::ForceCancelInteraction()
{
	switch (Interact.State)
	{
	case EInteractionState::Gameplay:
		return;

	case EInteractionState::Focusing:
	case EInteractionState::Focused:
	{
		if (Interact.ActiveProfile && !Interact.ActiveProfile->bAllowDamageCancel)
			return;

		CloseFocusPanel();

		// For no-camera focus, just restore directly
		if (Interact.ActiveProfile && !Interact.ActiveProfile->bMoveCamera)
		{
			RestoreCameraControl();
			SetInteractionState(EInteractionState::Gameplay);
			Interact.ActiveProfile = nullptr;
			Interact.ActiveTargetKey = FSkeletonKey();
			return;
		}

		// Fast camera exit
		float FastDuration = Interact.ActiveProfile
			? Interact.ActiveProfile->DamageCancelTransitionTime : 0.15f;
		Interact.CurrentTransitionDuration = FastDuration;
		SetInteractionState(EInteractionState::Unfocusing);
		break;
	}

	case EInteractionState::Unfocusing:
		return; // Already exiting

	case EInteractionState::Holding:
	{
		if (Interact.ActiveProfile && !Interact.ActiveProfile->bAllowDamageCancel)
			return;
		CancelHoldInteraction();
		break;
	}
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// INIT / CLEANUP (called from BeginPlay / EndPlay)
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::InitInteractionTrace()
{
	GetWorld()->GetTimerManager().SetTimer(
		InteractionTraceTimerHandle,
		this,
		&AFlecsCharacter::PerformInteractionTrace,
		0.1f, // 10 Hz
		true   // looping
	);
}

void AFlecsCharacter::CleanupInteraction()
{
	if (Interact.State != EInteractionState::Gameplay)
	{
		CloseFocusPanel();
		RestoreCameraControl();
		Interact.State = EInteractionState::Gameplay;
		Interact.ActiveProfile = nullptr;
		Interact.ActiveTargetKey = FSkeletonKey();
	}

	GetWorld()->GetTimerManager().ClearTimer(InteractionTraceTimerHandle);
	Interact.CurrentTarget = FSkeletonKey();
}

// ═══════════════════════════════════════════════════════════════════════════
// INTERACTION DETECTION (10 Hz Barrage raycast)
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::PerformInteractionTrace()
{
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC) return;

	UBarrageDispatch* Barrage = GetWorld()->GetSubsystem<UBarrageDispatch>();
	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!Barrage || !FlecsSubsystem) return;

	// Get camera viewpoint
	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	FVector Direction = CameraRotation.Vector();

	// Set up filters: detect MOVING layer objects, ignore self
	auto BroadPhaseFilter = Barrage->GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
	auto ObjectFilter = Barrage->GetDefaultLayerFilter(Layers::MOVING);
	FBarrageKey CharBarrageKey = Barrage->GetBarrageKeyFromSkeletonKey(CharacterKey);
	auto BodyFilter = Barrage->GetFilterToIgnoreSingleBody(CharBarrageKey);

	// Cast ray/sphere
	TSharedPtr<FHitResult> HitResult = MakeShared<FHitResult>();

	if (bUseSphereTrace)
	{
		Barrage->SphereCast(
			InteractionSphereRadius,
			MaxInteractionDistance,
			CameraLocation,
			Direction,
			HitResult,
			BroadPhaseFilter, ObjectFilter, BodyFilter);
	}
	else
	{
		Barrage->CastRay(
			CameraLocation,
			Direction * MaxInteractionDistance,
			BroadPhaseFilter, ObjectFilter, BodyFilter,
			HitResult);
	}

	FSkeletonKey NewTarget;
	FSkeletonKey NewCraftingHover;

	if (HitResult->bBlockingHit)
	{
		// Convert BodyID → BarrageKey → SkeletonKey → Flecs entity
		FBarrageKey HitBarrageKey = Barrage->GetBarrageKeyFromFHitResult(HitResult);
		FBLet Prim = Barrage->GetShapeRef(HitBarrageKey);
		if (FBarragePrimitive::IsNotNull(Prim))
		{
			FSkeletonKey HitKey = Prim->KeyOutOfBarrage;
			flecs::entity HitEntity = FlecsSubsystem->GetEntityForBarrageKey(HitKey);

			// Crafting station hover — parallel to FTagInteractable detection.
			// Runs independently so stations without InteractionProfile still publish hover state.
			// IMPORTANT: use Flecs entity id (not BarrageKey) — this matches the key used by
			// FlecsEntitySpawner::CreateSharedState and CraftingSnapshotFlushSystem's publish key.
			if (HitEntity.is_valid() && !HitEntity.has<FTagDead>() && HitEntity.has<FTagCraftingStation>())
			{
				NewCraftingHover = FSkeletonKey(static_cast<uint64>(HitEntity.id()));
			}

			if (HitEntity.is_valid() && HitEntity.has<FTagInteractable>() && !HitEntity.has<FTagDead>())
			{
				// Check interaction range from InteractionStatic
				const FInteractionStatic* InterStatic = HitEntity.try_get<FInteractionStatic>();
				float MaxRange = InterStatic ? InterStatic->MaxRange : MaxInteractionDistance;

				if (HitResult->Distance <= MaxRange)
				{
					// Check angle restriction
					bool bAngleOk = true;

					// Resolve angle constraint: per-instance override > prefab default
					bool bHasAngleRestriction = false;
					float AngleCosine = 0.f;
					FVector AngleDir = FVector::ForwardVector;

					const FInteractionAngleOverride* AngleOverride = HitEntity.try_get<FInteractionAngleOverride>();
					if (AngleOverride)
					{
						bHasAngleRestriction = true;
						AngleCosine = AngleOverride->AngleCosine;
						AngleDir = AngleOverride->Direction;
					}
					else if (InterStatic && InterStatic->bRestrictAngle)
					{
						bHasAngleRestriction = true;
						AngleCosine = InterStatic->AngleCosine;
						AngleDir = InterStatic->AngleDirection;
					}

					if (bHasAngleRestriction)
					{
						FQuat EntityRot = FQuat(FBarragePrimitive::OptimisticGetAbsoluteRotation(Prim));
						FVector WorldAngleDir = EntityRot.RotateVector(AngleDir);

						FVector EntityPos = FVector(FBarragePrimitive::GetPosition(Prim));
						FVector ToEntity = (EntityPos - CameraLocation).GetSafeNormal();

						float Dot = FVector::DotProduct(WorldAngleDir, ToEntity);
						bAngleOk = (Dot >= AngleCosine);

						UE_LOG(LogTemp, Verbose, TEXT("AngleCheck: EntityPos=%s CamPos=%s ToEntity=%s WorldDir=%s Dot=%.3f Cos=%.3f Ok=%d"),
							*EntityPos.ToString(), *CameraLocation.ToString(), *ToEntity.ToString(),
							*WorldAngleDir.ToString(), Dot, AngleCosine, bAngleOk);
					}

					if (bAngleOk)
					{
						NewTarget = HitKey;
					}
				}
			}
		}
	}

	// Fire event if target changed
	if (NewTarget != Interact.CurrentTarget)
	{
		Interact.CurrentTarget = NewTarget;

		// Cache interaction prompt (avoids cross-thread reads later)
		if (NewTarget.IsValid())
		{
			flecs::entity TargetEntity = FlecsSubsystem->GetEntityForBarrageKey(NewTarget);
			if (TargetEntity.is_valid())
			{
				const FEntityDefinitionRef* DefRef = TargetEntity.try_get<FEntityDefinitionRef>();
				if (DefRef && DefRef->Definition && DefRef->Definition->InteractionProfile)
				{
					Interact.CachedPrompt = DefRef->Definition->InteractionProfile->InteractionPrompt;
					Interact.CachedType = DefRef->Definition->InteractionProfile->InteractionType;
					Interact.CachedHoldDuration = DefRef->Definition->InteractionProfile->HoldDuration;
				}
				else
				{
					Interact.CachedPrompt = NSLOCTEXT("Interaction", "Fallback", "Press E");
					Interact.CachedType = EInteractionType::Instant;
					Interact.CachedHoldDuration = 0.f;
				}
			}
			UE_LOG(LogTemp, Log, TEXT("Interaction: Target acquired Key=%llu"), static_cast<uint64>(NewTarget));
		}
		else
		{
			Interact.CachedPrompt = FText::GetEmpty();
			Interact.CachedType = EInteractionType::Instant;
			Interact.CachedHoldDuration = 0.f;

			// Auto-close loot panel when target lost (walked away)
			if (IsLootOpen())
			{
				CloseLootPanel();
			}
		}

		OnInteractionTargetChanged(NewTarget.IsValid(), NewTarget);

		// Broadcast to message system (game thread — direct broadcast)
		if (UFlecsMessageSubsystem* MsgSub = UFlecsMessageSubsystem::Get(this))
		{
			FUIInteractionMessage InterMsg;
			InterMsg.bHasTarget = NewTarget.IsValid();
			InterMsg.TargetKey = NewTarget;
			InterMsg.InteractionType = static_cast<uint8>(Interact.CachedType);
			InterMsg.HoldDuration = Interact.CachedHoldDuration;
			if (NewTarget.IsValid())
			{
				flecs::entity E = FlecsSubsystem->GetEntityForBarrageKey(NewTarget);
				InterMsg.EntityId = E.is_valid() ? static_cast<int64>(E.id()) : 0;
			}
			MsgSub->BroadcastMessage(TAG_UI_Interaction, InterMsg);
		}
	}

	// Crafting hover target — independent of FTagInteractable detection.
	// Silent write-through (no broadcast/event); widgets poll via GetCraftingHoverTarget.
	Interact.CraftingHoverTarget = NewCraftingHover;

	// ─── Phase 4 — wrench-aware hover classification ─────────────
	// Recompute every trace tick (10 Hz). Cheap: hits the same FlecsSubsystem +
	// optional roster walk (≤15) + optional port loop (≤8). Game thread only.
	EWrenchHoverKind WrenchKind = EWrenchHoverKind::NotApplicable;
	int64 WrenchTargetId = 0;
	int32 WrenchPortIdx  = -1;

	if (HitResult->bBlockingHit && IsWrenchEquipped())
	{
		const FBarrageKey HitBarrageKey = Barrage->GetBarrageKeyFromFHitResult(HitResult);
		FBLet HitPrim = Barrage->GetShapeRef(HitBarrageKey);
		if (FBarragePrimitive::IsNotNull(HitPrim))
		{
			const FSkeletonKey HitKey = HitPrim->KeyOutOfBarrage;
			flecs::entity Hit = FlecsSubsystem->GetEntityForBarrageKey(HitKey);
			if (Hit.is_valid() && Hit.is_alive() && !Hit.has<FTagDead>())
			{
				// Case A: hit a bonded child → Swappable / Rigid Functional
				if (const FMultiblockChildOf* Back = Hit.try_get<FMultiblockChildOf>())
				{
					flecs::world* World = FlecsSubsystem->GetFlecsWorld();
					if (World)
					{
						flecs::entity Anchor = World->entity(static_cast<flecs::entity_t>(Back->AnchorEntityId));
						if (Anchor.is_alive())
						{
							if (const FMultiblockChildren* Roster = Anchor.try_get<FMultiblockChildren>())
							{
								for (int32 i = 0; i < Roster->ChildCount; ++i)
								{
									if (Roster->ChildSlots[i].ChildEntityId == static_cast<int64>(Hit.id()))
									{
										WrenchKind = (Roster->ChildSlots[i].bSwappable != 0)
											? EWrenchHoverKind::SwappableFunctional
											: EWrenchHoverKind::RigidFunctional;
										// Use Flecs entity id for the target key (matches CraftingHoverTarget).
										WrenchTargetId = static_cast<int64>(Hit.id());
										break;
									}
								}
							}
						}
					}
				}
				// Case B: hit the anchor itself → Anchor (rigid) + maybe an empty extension port nearby
				else if (Hit.has<FTagCraftingStation>() && Hit.has<FMultiblockChildren>())
				{
					WrenchKind = EWrenchHoverKind::Anchor;
					WrenchTargetId = static_cast<int64>(Hit.id());

					// Extension-port-empty proximity test: closest unoccupied port pose within 100 cm.
					const FMultiblockChildren* Roster = Hit.try_get<FMultiblockChildren>();
					const FMultiblockExtensions* Ext = Hit.try_get<FMultiblockExtensions>();
					const UFlecsMultiblockBlueprint* BP = FlecsMultiblockRuntime::ResolveBlueprintForStation(Hit);
					if (Roster && BP && BP->ExtensionPorts.Num() > 0)
					{
						FVector AnchorPos;
						FQuat AnchorQuat;
						if (GetEntityWorldTransform(HitKey, AnchorPos, AnchorQuat))
						{
							const FQuat SnappedQuat(FRotator(0.f, static_cast<float>(Roster->AnchorYawSnappedDeg), 0.f).Quaternion());
							float ClosestSq = 100.f * 100.f;  // 100 cm hover radius
							for (int32 p = 0; p < BP->ExtensionPorts.Num() && p < 8; ++p)
							{
								const bool bOccupied = (Ext && Ext->PortOccupants[p] != 0);
								const FVector PortPos = AnchorPos + SnappedQuat.RotateVector(BP->ExtensionPorts[p].RelativeOffset);
								const float DistSq = FVector::DistSquared(PortPos, HitResult->Location);
								if (DistSq < ClosestSq)
								{
									ClosestSq = DistSq;
									WrenchPortIdx = p;
									WrenchKind = bOccupied
										? EWrenchHoverKind::OccupiedExtensionPort
										: EWrenchHoverKind::EmptyExtensionPort;
									// For empty/occupied port hover, target is still the station entity id.
									WrenchTargetId = static_cast<int64>(Hit.id());
								}
							}
						}
					}
				}
			}
		}
	}

	Interact.WrenchHoverKind     = WrenchKind;
	Interact.WrenchHoverTargetId = WrenchTargetId;
	Interact.WrenchHoverPortIndex = WrenchPortIdx;
}

FText AFlecsCharacter::GetInteractionPrompt() const
{
	return Interact.CachedPrompt;
}
