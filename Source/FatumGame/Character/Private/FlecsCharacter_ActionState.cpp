// Action State System implementation for AFlecsCharacter.
// SetGameBit, ClearGameBit, HandleStateCanceled, InitReloadListener.

#include "FlecsCharacter.h"
#include "FatumMovementComponent.h"
#include "FlecsCharacterTypes.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"
#include "FlecsWeaponLibrary.h"

uint64 AFlecsCharacter::GetFullActionState() const
{
	// NOTE: SimState may lag by up to one sim tick (~16ms). Acceptable for gameplay state queries.
	uint64 GameState = GameActionState.load(std::memory_order_relaxed);
	uint64 SimState = StateAtomics ? StateAtomics->ReadSimState() : 0;
	return GameState | SimState;
}

bool AFlecsCharacter::SetGameBit(uint64 Bit)
{
	check(IsInGameThread());
	checkf(FindActionRule(Bit) != nullptr, TEXT("SetGameBit: bit 0x%llx has no rule in GActionRules"), Bit);

	uint64 Current = GameActionState.load(std::memory_order_relaxed);
	uint64 SimState = StateAtomics ? StateAtomics->ReadSimState() : 0;
	uint64 FullState = Current | SimState;

	auto Result = TryEnterState(FullState, Bit);
	if (!Result.bSuccess)
		return false;

	// Extract only game-thread-owned bits from the result.
	// Single SimState read — no double-read race.
	uint64 NewGameState = Result.NewState & ~SimState;

	GameActionState.store(NewGameState, std::memory_order_relaxed);

	// Handle side effects for canceled states (only game-thread-owned bits)
	if (Result.CanceledBits != 0)
	{
		uint64 GameCanceled = Result.CanceledBits & Current;
		if (GameCanceled != 0)
			HandleStateCanceled(GameCanceled);
	}

	return true;
}

void AFlecsCharacter::ClearGameBit(uint64 Bit)
{
	check(IsInGameThread());

	uint64 Current = GameActionState.load(std::memory_order_relaxed);
	if (!(Current & Bit))
		return; // Not set

	uint64 NewState = Current & ~Bit;

	// Process deferred transitions (e.g., exit Firing + SprintHeld → enter Sprinting)
	uint64 InputState = InputAtomics ? InputAtomics->ReadInputState() : 0;
	NewState = ProcessDeferredTransitions(NewState, Bit, InputState);

	GameActionState.store(NewState, std::memory_order_relaxed);
}

void AFlecsCharacter::HandleStateCanceled(uint64 CanceledBits)
{
	// Sprint canceled — tell movement component to stop sprinting
	if (HasBit(CanceledBits, ActionBit::Sprinting))
	{
		if (FatumMovement) FatumMovement->RequestSprint(false);
		if (InputAtomics) InputAtomics->Sprinting.Write(false);
	}

	// Firing canceled — stop weapon fire
	if (HasBit(CanceledBits, ActionBit::Firing))
	{
		if (ActiveWeaponEntityId != 0)
			StopFiringWeapon();
	}

	// ADS canceled — clear wants flag for TickADS
	if (HasBit(CanceledBits, ActionBit::ADS))
	{
		RecoilState.bWantsADS = false;
	}

	// Reloading canceled — signal cancel to sim thread
	if (HasBit(CanceledBits, ActionBit::Reloading))
	{
		if (ActiveWeaponEntityId != 0)
			UFlecsWeaponLibrary::ToggleReload(this, ActiveWeaponEntityId);
	}
}

bool AFlecsCharacter::IsFireHeld() const
{
	return InputAtomics && HasBit(InputAtomics->ReadInputState(), InputBit::FireHeld);
}

bool AFlecsCharacter::IsSprintKeyHeld() const
{
	return InputAtomics && HasBit(InputAtomics->ReadInputState(), InputBit::SprintHeld);
}

void AFlecsCharacter::InitWeaponListeners()
{
	UFlecsMessageSubsystem* MsgSub = UFlecsMessageSubsystem::Get(this);
	if (!ensure(MsgSub)) return;

	MsgSub->RegisterListener<FUIReloadMessage>(
		TAG_UI_Reload, this,
		[this](FGameplayTag Tag, const FUIReloadMessage& Msg)
		{
			if (Msg.WeaponEntityId != ActiveWeaponEntityId) return;

			if (!Msg.bStarted)
			{
				// Reload complete/cancelled — clear Reloading bit, restore sprint if held
				ClearGameBit(ActionBit::Reloading);
				if (HasBit(GameActionState.load(std::memory_order_relaxed), ActionBit::Sprinting))
				{
					if (InputAtomics) InputAtomics->Sprinting.Write(true);
					if (FatumMovement) FatumMovement->RequestSprint(true);
				}
			}
		});
}
