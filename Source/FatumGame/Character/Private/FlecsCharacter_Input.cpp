// Input binding and handler implementation for AFlecsCharacter.
// SetupPlayerInputComponent, CreatePlayerInputComponent, and all input action callbacks.

#include "FlecsCharacter.h"
#include "FatumInputComponent.h"
#include "FatumInputTags.h"
#include "FatumMovementComponent.h"
#include "FlecsWeaponProfile.h"
#include "FlecsEntityDefinition.h"
#include "FlecsContainerLibrary.h"
#include "FlecsItemDefinition.h"
#include "Camera/CameraComponent.h"
#include "InputActionValue.h"
#include "GameFramework/Controller.h"
#include "Engine/Engine.h"

// ═══════════════════════════════════════════════════════════════════════════
// ENHANCED INPUT SETUP
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UFatumInputComponent* FatumInput = CastChecked<UFatumInputComponent>(PlayerInputComponent);
	checkf(InputConfig, TEXT("AFlecsCharacter: InputConfig is not set! Assign a UFatumInputConfig Data Asset."));

	FatumInput->BindNativeAction(InputConfig, TAG_Input_Move,      ETriggerEvent::Triggered, this, &AFlecsCharacter::Move);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Move,      ETriggerEvent::Completed, this, &AFlecsCharacter::Move);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Look,      ETriggerEvent::Triggered, this, &AFlecsCharacter::Look);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Jump,      ETriggerEvent::Started,   this, &AFlecsCharacter::OnJumpStarted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Jump,      ETriggerEvent::Completed, this, &AFlecsCharacter::OnJumpCompleted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Fire,      ETriggerEvent::Started,   this, &AFlecsCharacter::StartFire);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Fire,      ETriggerEvent::Completed, this, &AFlecsCharacter::StopFire);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Interact,  ETriggerEvent::Started,   this, &AFlecsCharacter::OnSpawnItem);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Interact,  ETriggerEvent::Completed, this, &AFlecsCharacter::OnInteractReleased);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Destroy,   ETriggerEvent::Started,   this, &AFlecsCharacter::OnDestroyItem);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Inventory, ETriggerEvent::Started,   this, &AFlecsCharacter::ToggleInventory);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Cancel,    ETriggerEvent::Started,   this, &AFlecsCharacter::OnInteractCancel);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Sprint,    ETriggerEvent::Started,   this, &AFlecsCharacter::OnSprintStarted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Sprint,    ETriggerEvent::Completed, this, &AFlecsCharacter::OnSprintCompleted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Reload,    ETriggerEvent::Started,   this, &AFlecsCharacter::OnReload);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_ADS,       ETriggerEvent::Started,   this, &AFlecsCharacter::OnADSStarted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_ADS,       ETriggerEvent::Completed, this, &AFlecsCharacter::OnADSCompleted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Crouch,    ETriggerEvent::Started,   this, &AFlecsCharacter::OnCrouchStarted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Crouch,    ETriggerEvent::Completed, this, &AFlecsCharacter::OnCrouchCompleted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Prone,     ETriggerEvent::Started,   this, &AFlecsCharacter::OnProneStarted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Prone,     ETriggerEvent::Completed, this, &AFlecsCharacter::OnProneCompleted);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Ability1,  ETriggerEvent::Started,   this, &AFlecsCharacter::OnAbility1Started);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Ability1,  ETriggerEvent::Completed, this, &AFlecsCharacter::OnAbility1Completed);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Ability2,  ETriggerEvent::Started,   this, &AFlecsCharacter::OnAbility2Started);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_Ability3,  ETriggerEvent::Started,   this, &AFlecsCharacter::OnTelekinesisToggle);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_TKThrow,   ETriggerEvent::Started,   this, &AFlecsCharacter::OnTelekinesisThrow);
	FatumInput->BindNativeAction(InputConfig, TAG_Input_TKScroll,  ETriggerEvent::Triggered, this, &AFlecsCharacter::OnTelekinesisScroll);
}

UInputComponent* AFlecsCharacter::CreatePlayerInputComponent()
{
	return NewObject<UFatumInputComponent>(this, UFatumInputComponent::StaticClass(), TEXT("FatumInputComponent"));
}

// ═══════════════════════════════════════════════════════════════════════════
// INPUT HANDLERS (thin routing — delegates to CMC / Combat / Interaction / Test)
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr && InputAtomics.IsValid())
	{
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// Build world-space direction (not normalized — magnitude = stick tilt)
		FVector WorldDir = ForwardDirection * MovementVector.Y + RightDirection * MovementVector.X;

		// Write to atomics (lock-free, latest-wins) — sim thread reads in PrepareCharacterStep.
		InputAtomics->DirX.Write(static_cast<float>(WorldDir.X));
		InputAtomics->DirZ.Write(static_cast<float>(WorldDir.Y));
	}
}

void AFlecsCharacter::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// ADS sensitivity scaling
		if (RecoilState.ADSAlpha > 0.f && RecoilState.CachedProfile)
		{
			float SensMul = RecoilState.CachedProfile->ADSSensitivityMultiplier;
			if (SensMul <= 0.f)
			{
				// Auto-compute from FOV ratio (focal length formula)
				float EffectiveBaseFOV = BaseFOV + (FatumMovement ? FatumMovement->GetCurrentFOVOffset() : 0.f);
				float BaseFOVRad = FMath::DegreesToRadians(EffectiveBaseFOV * 0.5f);
				float ADSFOVRad = FMath::DegreesToRadians(RecoilState.CachedProfile->ADSFOV * 0.5f);
				SensMul = FMath::Tan(ADSFOVRad) / FMath::Tan(BaseFOVRad);
			}
			float EffectiveMul = FMath::Lerp(1.f, SensMul, RecoilState.ADSAlpha);
			LookAxisVector *= EffectiveMul;
		}

		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);

		// Accumulate raw mouse delta for weapon inertia (recoil-free).
		// Negate pitch: LookAxisVector.Y sign convention differs from ControlRotation.Pitch.
		RecoilState.RawMouseDelta += FVector2D(-LookAxisVector.Y, LookAxisVector.X);  // (Pitch, Yaw)
	}
}

void AFlecsCharacter::OnSprintStarted(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->SetInputBit(InputBit::SprintHeld);

	// Rule table handles all blocks (Firing, ADS, Mantling, etc.)
	if (SetGameBit(ActionBit::Sprinting))
	{
		if (InputAtomics) InputAtomics->Sprinting.Write(true);  // dual-write for sim thread compat
		if (FatumMovement) FatumMovement->RequestSprint(true);
	}
}

void AFlecsCharacter::OnSprintCompleted(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->ClearInputBit(InputBit::SprintHeld);
	ClearGameBit(ActionBit::Sprinting);
	if (InputAtomics) InputAtomics->Sprinting.Write(false);
	if (FatumMovement) FatumMovement->RequestSprint(false);
}

void AFlecsCharacter::OnJumpStarted(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->JumpPressed.Fire();
}

void AFlecsCharacter::OnJumpCompleted(const FInputActionValue& Value)
{
	// Jump is one-shot (consumed by sim thread), no release needed
}

void AFlecsCharacter::OnCrouchStarted(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->SetInputBit(InputBit::CrouchHeld);
	if (InputAtomics) InputAtomics->CrouchHeld.Write(true);  // dual-write
	if (SetGameBit(ActionBit::Crouching))
	{
		if (FatumMovement) FatumMovement->RequestCrouch(true);
	}
}

void AFlecsCharacter::OnCrouchCompleted(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->ClearInputBit(InputBit::CrouchHeld);
	if (InputAtomics) InputAtomics->CrouchHeld.Write(false);  // dual-write
	ClearGameBit(ActionBit::Crouching);
	if (FatumMovement) FatumMovement->RequestCrouch(false);
}

void AFlecsCharacter::OnProneStarted(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->SetInputBit(InputBit::ProneHeld);
	if (SetGameBit(ActionBit::Prone))
	{
		if (FatumMovement) FatumMovement->RequestProne(true);
	}
}

void AFlecsCharacter::OnProneCompleted(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->ClearInputBit(InputBit::ProneHeld);
	ClearGameBit(ActionBit::Prone);
	if (FatumMovement) FatumMovement->RequestProne(false);
}

void AFlecsCharacter::OnAbility1Started(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->BlinkHeld.Write(true);
}

void AFlecsCharacter::OnAbility1Completed(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->BlinkHeld.Write(false);
}

void AFlecsCharacter::OnAbility2Started(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->Ability2Pressed.Fire();
}

void AFlecsCharacter::OnTelekinesisToggle(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->TelekinesisToggle.Fire();
}

void AFlecsCharacter::OnTelekinesisThrow(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->TelekinesisThrow.Fire();
}

void AFlecsCharacter::OnTelekinesisScroll(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->TelekinesisScroll.Write(Value.Get<float>());
}

void AFlecsCharacter::StartFire(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->SetInputBit(InputBit::FireHeld);

	// Rule table handles blocks (Mantling, Climbing, LedgeHang, Dead, WeaponRetracted, etc.)
	// CanceledOnEntry handles Sprinting cancel
	if (!SetGameBit(ActionBit::Firing))
		return;

	// Sprint was auto-canceled via HandleStateCanceled if it was active.
	// Deferred transition will restore it on ClearGameBit(Firing) + SprintHeld.

	// Sync sprint cancel to sim thread + movement component
	if (!HasBit(GameActionState.load(std::memory_order_relaxed), ActionBit::Sprinting))
	{
		if (InputAtomics) InputAtomics->Sprinting.Write(false);
		if (FatumMovement) FatumMovement->RequestSprint(false);
	}

	if (TestWeaponDefinition)
	{
		if (TestWeaponEntityId == 0)
		{
			SpawnAndEquipTestWeapon();
			bPendingFireAfterSpawn = true;
			return;
		}
		StartFiringWeapon();
	}
	else
	{
		FireProjectile();
	}
}

void AFlecsCharacter::StopFire(const FInputActionValue& Value)
{
	if (InputAtomics) InputAtomics->ClearInputBit(InputBit::FireHeld);
	bPendingFireAfterSpawn = false;

	if (TestWeaponEntityId != 0)
		StopFiringWeapon();

	// ClearGameBit processes deferred transitions:
	// if SprintHeld → auto-enters Sprinting
	ClearGameBit(ActionBit::Firing);

	// Sync deferred sprint restore to sim thread + movement component
	if (HasBit(GameActionState.load(std::memory_order_relaxed), ActionBit::Sprinting))
	{
		if (InputAtomics) InputAtomics->Sprinting.Write(true);
		if (FatumMovement) FatumMovement->RequestSprint(true);
	}
}

void AFlecsCharacter::OnReload(const FInputActionValue& Value)
{
	if (TestWeaponEntityId == 0) return;

	uint64 State = GetFullActionState();
	if (HasBit(State, ActionBit::Reloading))
	{
		// Already reloading — send cancel request to sim thread.
		// Do NOT clear the bit here. The sim thread is authoritative.
		// InitReloadListener will clear the bit when sim confirms completion/cancel.
		ReloadTestWeapon();
	}
	else
	{
		// Start reload — rule table cancels Sprint and Firing
		if (!SetGameBit(ActionBit::Reloading))
			return;

		// Sync sprint cancel to sim thread + movement component
		if (!HasBit(GameActionState.load(std::memory_order_relaxed), ActionBit::Sprinting))
		{
			if (InputAtomics) InputAtomics->Sprinting.Write(false);
			if (FatumMovement) FatumMovement->RequestSprint(false);
		}

		ReloadTestWeapon();
	}
}

void AFlecsCharacter::OnSpawnItem(const FInputActionValue& Value)
{
	// E closes loot panel if open
	if (IsLootOpen())
	{
		CloseLootPanel();
		return;
	}

	// If in an interaction state, route to state machine (E exits Focus, etc.)
	if (Interact.State != EInteractionState::Gameplay)
	{
		HandleInteractionInput();
		return;
	}

	// If we have an interaction target, begin interaction
	if (Interact.CurrentTarget.IsValid())
	{
		Interact.bInteractKeyHeld = true;
		HandleInteractionInput();
		return;
	}

	// If container testing is configured, use container mode
	if (TestContainerDefinition && TestItemDefinition && TestItemDefinition->ItemDefinition)
	{
		if (!TestContainerKey.IsValid())
		{
			SpawnTestContainer();
		}
		else
		{
			AddItemToTestContainer();
		}
	}
	else if (TestItemDefinition && TestItemDefinition->ItemDefinition && InventoryEntityId != 0)
	{
		int32 Added = 0;
		UFlecsContainerLibrary::AddItemToContainer(this, InventoryEntityId, TestItemDefinition, 1, Added, false);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan,
				FString::Printf(TEXT("Added '%s' to inventory"), *TestItemDefinition->GetName()));
		}
	}
	else
	{
		SpawnTestEntity();
	}
}

void AFlecsCharacter::OnInteractReleased(const FInputActionValue& Value)
{
	HandleInteractionRelease();
}

void AFlecsCharacter::OnInteractCancel(const FInputActionValue& Value)
{
	HandleInteractionCancel();
}

void AFlecsCharacter::OnDestroyItem(const FInputActionValue& Value)
{
	if (TestContainerDefinition && TestContainerKey.IsValid())
	{
		RemoveAllItemsFromTestContainer();
	}
	else
	{
		DestroyLastSpawnedEntity();
	}
}
