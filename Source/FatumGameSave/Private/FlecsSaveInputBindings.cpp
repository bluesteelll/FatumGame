// UFlecsSaveInputBindings — F5 Quicksave / F9 Quickload key bindings owned by FatumGameSave.

#include "FlecsSaveInputBindings.h"
#include "FlecsSaveLog.h"
#include "FlecsSaveSubsystem.h"

#include "Components/InputComponent.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "UObject/UObjectGlobals.h"

// ═══════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════

void UFlecsSaveInputBindings::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Subsystem may exist before any world is loaded. Hook PostLoadMapWithWorld so we
	// rebind on every map change (player controller is recreated per map).
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UFlecsSaveInputBindings::OnPostLoadMapWithWorld);

	UE_LOG(LogFlecsSave, Log, TEXT("UFlecsSaveInputBindings: Initialize — hooked PostLoadMapWithWorld"));
}

void UFlecsSaveInputBindings::Deinitialize()
{
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}
	if (PollHandle.IsValid())
	{
		FTSTicker::RemoveTicker(PollHandle);
		PollHandle.Reset();
	}
	bBindingsInstalled = false;
	BoundPC.Reset();

	UE_LOG(LogFlecsSave, Log, TEXT("UFlecsSaveInputBindings: Deinitialize"));
	Super::Deinitialize();
}

// ═══════════════════════════════════════════════════════════════
// BINDING — defer to next tick, retry until PC is available
// ═══════════════════════════════════════════════════════════════

void UFlecsSaveInputBindings::OnPostLoadMapWithWorld(UWorld* World)
{
	if (!World || !World->IsGameWorld())
	{
		// Editor preview / cooker / cleanup worlds — never bind.
		return;
	}

	// Reset state for the new map. The PC will be recreated.
	bBindingsInstalled = false;
	BoundPC.Reset();

	// Cancel any prior poll loop in case PostLoadMapWithWorld fires before the previous
	// poll succeeded (rare — but safe).
	if (PollHandle.IsValid())
	{
		FTSTicker::RemoveTicker(PollHandle);
		PollHandle.Reset();
	}

	// Poll on the next core tick — keep retrying until the local player controller appears
	// (it may take 1-3 ticks after BeginPlay before the local player's PC is created and its
	// InputComponent exists). Bounded to ~5 seconds to avoid leaking the ticker on dedicated
	// servers / launch errors.
	TWeakObjectPtr<UFlecsSaveInputBindings> WeakThis(this);
	TWeakObjectPtr<UWorld> WeakWorld(World);
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	PollHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda(
			[WeakThis, WeakWorld, Deadline](float /*DeltaT*/) -> bool
			{
				UFlecsSaveInputBindings* Self = WeakThis.Get();
				UWorld* W = WeakWorld.Get();
				if (!Self || !W)
				{
					return false; // stop polling, refs gone
				}

				// Find the first local player controller in the world. GameInstance owns
				// the local players; their PCs are spawned per-world.
				APlayerController* PC = W->GetFirstPlayerController();
				if (PC && PC->InputComponent)
				{
					if (Self->TryInstallBindings(PC))
					{
						Self->PollHandle.Reset();
						return false; // stop polling, bound
					}
				}

				if (FPlatformTime::Seconds() > Deadline)
				{
					UE_LOG(LogFlecsSave, Warning,
						TEXT("UFlecsSaveInputBindings: gave up after 5s — no local PC with InputComponent"));
					Self->PollHandle.Reset();
					return false;
				}
				return true; // keep polling next tick
			}),
		/*Delay=*/ 0.0f);
}

bool UFlecsSaveInputBindings::TryInstallBindings(APlayerController* PC)
{
	check(PC);
	UInputComponent* IC = PC->InputComponent;
	if (!IC)
	{
		return false;
	}

	// Idempotency: if we already bound to this exact PC, do not duplicate.
	if (bBindingsInstalled && BoundPC.Get() == PC)
	{
		return true;
	}

	// Bind raw F5/F9. EInputEvent::IE_Pressed fires on key-down; release events are
	// not bound (quicksave/quickload are one-shot).
	IC->BindKey(EKeys::F5, EInputEvent::IE_Pressed, this, &UFlecsSaveInputBindings::HandleQuickSave);
	IC->BindKey(EKeys::F9, EInputEvent::IE_Pressed, this, &UFlecsSaveInputBindings::HandleQuickLoad);

	BoundPC = PC;
	bBindingsInstalled = true;

	UE_LOG(LogFlecsSave, Log,
		TEXT("UFlecsSaveInputBindings: bound F5/F9 to PC '%s'"),
		*PC->GetName());
	return true;
}

// ═══════════════════════════════════════════════════════════════
// HANDLERS
// ═══════════════════════════════════════════════════════════════

void UFlecsSaveInputBindings::HandleQuickSave()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		UE_LOG(LogFlecsSave, Warning, TEXT("HandleQuickSave: no GameInstance"));
		return;
	}
	UFlecsSaveSubsystem* Sub = GI->GetSubsystem<UFlecsSaveSubsystem>();
	if (!Sub)
	{
		UE_LOG(LogFlecsSave, Warning, TEXT("HandleQuickSave: UFlecsSaveSubsystem missing"));
		return;
	}

	const ESaveResult Result = Sub->RequestQuicksave();
	UE_LOG(LogFlecsSave, Display, TEXT("Quicksave triggered (result=%d)"), (int32)Result);
}

void UFlecsSaveInputBindings::HandleQuickLoad()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		UE_LOG(LogFlecsSave, Warning, TEXT("HandleQuickLoad: no GameInstance"));
		return;
	}
	UFlecsSaveSubsystem* Sub = GI->GetSubsystem<UFlecsSaveSubsystem>();
	if (!Sub)
	{
		UE_LOG(LogFlecsSave, Warning, TEXT("HandleQuickLoad: UFlecsSaveSubsystem missing"));
		return;
	}

	const ELoadResult Result = Sub->RequestQuickload();
	UE_LOG(LogFlecsSave, Display, TEXT("Quickload triggered (result=%d)"), (int32)Result);
}
