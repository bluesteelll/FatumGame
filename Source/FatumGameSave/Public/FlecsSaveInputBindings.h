// UFlecsSaveInputBindings — installs raw F5 (Quicksave) / F9 (Quickload) key bindings
// onto the local player's InputComponent without touching AFlecsCharacter.
//
// WHY NOT ON THE CHARACTER:
// FatumGame.Build.cs explicitly forbids depending on FatumGameSave (it would create a
// circular dependency since FatumGameSave -> FatumGame). Hosting the F5/F9 handlers on
// AFlecsCharacter would require that dep. Instead this UGameInstanceSubsystem hooks
// FCoreUObjectDelegates::PostLoadMapWithWorld, finds the local PlayerController, and
// binds raw FKeys to its UInputComponent — bypassing the Enhanced Input data-asset
// path as the plan requested (no IA_QuickSave / IA_QuickLoad needed).
//
// NOTE: F5/F9 fire unconditionally even when a UI panel has focus (Phase 6 TODO).
// Once the UI captures input through its own mapping context, gate these bindings
// against UGameViewportClient::GetGameInstance()->GetPrimaryPlayerController()->bShowMouseCursor.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/WeakObjectPtr.h"
#include "FlecsSaveInputBindings.generated.h"

class APlayerController;
class UInputComponent;
class UWorld;

UCLASS()
class FATUMGAMESAVE_API UFlecsSaveInputBindings : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	/** Game-thread callback from FCoreUObjectDelegates::PostLoadMapWithWorld.
	 *  Defers binding by one frame so PlayerController has had a chance to spawn. */
	void OnPostLoadMapWithWorld(UWorld* World);

	/** Bind F5/F9 to the local player controller's InputComponent. Idempotent —
	 *  uses bBindingsInstalled + a weak-ptr-equality check to avoid duplicate binds
	 *  when the PC respawns (e.g. seamless travel without a map reload). */
	bool TryInstallBindings(APlayerController* PC);

	/** F5 handler — calls UFlecsSaveSubsystem::RequestQuicksave. */
	void HandleQuickSave();

	/** F9 handler — calls UFlecsSaveSubsystem::RequestQuickload. */
	void HandleQuickLoad();

	/** FTSTicker handle for the deferred TryInstallBindings poll. */
	FTSTicker::FDelegateHandle PollHandle;

	/** Weak ref to the PC that last received our bindings — prevents double-bind across
	 *  reloads while also detecting respawned PCs that need fresh bindings. */
	TWeakObjectPtr<APlayerController> BoundPC;

	/** True after a successful bind on the current PC. Cleared on PC swap or Deinitialize. */
	bool bBindingsInstalled = false;

	/** PostLoadMapWithWorld delegate handle (for clean removal in Deinitialize). */
	FDelegateHandle PostLoadMapHandle;
};
