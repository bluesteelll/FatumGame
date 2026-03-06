// Ability lifecycle manager — tick active abilities + charge recharge.
// Priority model: exclusive > always-on > active > recharge.
// Activation checks remain in PrepareCharacterStep to preserve priority order
// (mantle > jump > slide).

#include "AbilityLifecycleManager.h"
#include "AbilityTickFunctions.h"
#include "FlecsAbilityTypes.h"
#include "FlecsAbilityStates.h"
#include "FlecsMovementStatic.h"
#include "FlecsArtillerySubsystem.h"
#include "FWorldSimOwner.h"
#include "FlecsResourceTypes.h"

/** Dispatch a tick function for a given slot. Handles result (deactivation, consume). */
static void DispatchSlotTick(FAbilityTickContext& Ctx, FAbilitySystem* AbilSys, int32 i,
                              float DeltaTime, FAbilityTickResults& Results)
{
	FAbilitySlot& Slot = AbilSys->Slots[i];
	int32 TypeIdx = static_cast<int32>(Slot.TypeId);
	checkf(TypeIdx > 0 && TypeIdx < static_cast<int32>(EAbilityTypeId::MAX),
		TEXT("TickAbilities: invalid TypeId %d in slot %d"), TypeIdx, i);

	FAbilityTickFn TickFn = GAbilityTickFunctions[TypeIdx];
	checkf(TickFn, TEXT("TickAbilities: no tick function for TypeId %d"), TypeIdx);

	EAbilityTickResult Result = TickFn(Ctx, Slot);

	switch (Result)
	{
	case EAbilityTickResult::Continue:
		Slot.PhaseTimer += DeltaTime;
		// Sustain drain (channeled abilities) — deplete → force deactivate, NO refund.
		// Intentional: ability tick runs BEFORE drain check, so the last frame still executes.
		if (Slot.HasSustainCosts())
		{
			FResourcePools* Pools = Ctx.Entity.try_get_mut<FResourcePools>();
			if (Pools && !ApplySustainDrain(*Pools, Slot, DeltaTime))
			{
				// Clean up ability-specific state before force-deactivating
				switch (Slot.TypeId)
				{
				case EAbilityTypeId::Slide:
					if (auto* S = Ctx.Entity.try_get_mut<FSlideState>()) S->Reset();
					break;
				case EAbilityTypeId::Mantle:
					if (auto* M = Ctx.Entity.try_get_mut<FMantleState>()) M->Reset();
					break;
				case EAbilityTypeId::Blink:
					if (auto* B = Ctx.Entity.try_get_mut<FBlinkState>()) { B->State = 0; B->bTeleportedThisFrame = false; }
					break;
				default: break;
				}
				AbilSys->DeactivateSlot(i);
			}
		}
		break;

	case EAbilityTickResult::End:
		// Partial refund on voluntary end (e.g., cancel channeled ability early)
		if (Slot.DeactivationRefund > 0.f && Slot.ActivationCostCount > 0)
		{
			FResourcePools* Pools = Ctx.Entity.try_get_mut<FResourcePools>();
			if (Pools)
			{
				for (int32 c = 0; c < Slot.ActivationCostCount; ++c)
				{
					const FAbilityCostEntry& Cost = Slot.ActivationCosts[c];
					if (Cost.IsValid())
						Pools->Refund(Cost.ResourceType, Cost.Amount * Slot.DeactivationRefund);
				}
			}
		}
		AbilSys->DeactivateSlot(i);
		break;

	case EAbilityTickResult::EndAndConsume:
		// No refund on consume (ability completed its purpose)
		AbilSys->DeactivateSlot(i);
		if (Slot.TypeId == EAbilityTypeId::Slide)
		{
			Results.bJumpConsumed = true;
		}
		break;
	}
}

FAbilityTickResults TickAbilities(
	flecs::entity Entity,
	FBCharacterBase* FBChar,
	const FMovementStatic* MS,
	FCharacterPhysBridge& Bridge,
	float DeltaTime,
	float VelocityScale,
	float DirX, float DirZ,
	bool bJumpPressed,
	bool bCrouchHeld,
	bool bSprinting,
	bool bCrouchEdge,
	bool bOnGround,
	const FCharacterInputAtomics* Input,
	UBarrageDispatch* Barrage,
	FSkeletonKey CharacterKey,
	FCharacterSimState* SimState,
	TArray<FCharacterPhysBridge>* CharacterBridges)
{
	FAbilityTickResults Results;

	FAbilitySystem* AbilSys = Entity.try_get_mut<FAbilitySystem>();
	if (!AbilSys || AbilSys->SlotCount == 0) return Results;

	// Build context once, shared across all ability ticks
	FAbilityTickContext Ctx;
	Ctx.Entity = Entity;
	Ctx.FBChar = FBChar;
	Ctx.MovementStatic = MS;
	Ctx.Bridge = &Bridge;
	Ctx.DeltaTime = DeltaTime;
	Ctx.VelocityScale = VelocityScale;
	Ctx.DirX = DirX;
	Ctx.DirZ = DirZ;
	Ctx.bJumpPressed = bJumpPressed;
	Ctx.bCrouchHeld = bCrouchHeld;
	Ctx.bSprinting = bSprinting;
	Ctx.bCrouchEdge = bCrouchEdge;
	Ctx.bOnGround = bOnGround;
	Ctx.Input = Input;
	Ctx.Barrage = Barrage;
	Ctx.CharacterKey = CharacterKey;
	Ctx.SimState = SimState;
	Ctx.CharacterBridges = CharacterBridges;

	// ── Phase 0: Check exclusive abilities ──
	// If any exclusive ability is active, tick ONLY that one (skip Phase 1+2).
	// Phase 3 (cooldown/recharge) still runs so other abilities recover while exclusive is active.
	{
		bool bExclusiveHandled = false;
		for (int32 i = 0; i < AbilSys->SlotCount; ++i)
		{
			if (!AbilSys->IsSlotActive(i)) continue;
			if (!AbilSys->Slots[i].bExclusive) continue;

			DispatchSlotTick(Ctx, AbilSys, i, DeltaTime, Results);
			bExclusiveHandled = true;
			break;
		}

		if (!bExclusiveHandled)
		{
			// ── Phase 1: Tick always-on abilities (regardless of Phase) ──
			for (int32 i = 0; i < AbilSys->SlotCount; ++i)
			{
				FAbilitySlot& Slot = AbilSys->Slots[i];
				if (!Slot.bAlwaysTick) continue;
				if (Slot.IsEmpty()) continue;

				DispatchSlotTick(Ctx, AbilSys, i, DeltaTime, Results);
			}

			// ── Phase 2: Tick active non-always-on abilities ──
			for (int32 i = 0; i < AbilSys->SlotCount; ++i)
			{
				if (!AbilSys->IsSlotActive(i)) continue;
				if (AbilSys->Slots[i].bAlwaysTick) continue; // already ticked in Phase 1

				DispatchSlotTick(Ctx, AbilSys, i, DeltaTime, Results);
			}
		}
	}

	// ── Phase 3: Cooldown tick + charge recharge (inactive slots) ──
	// Always runs, even during exclusive abilities.
	for (int32 i = 0; i < AbilSys->SlotCount; ++i)
	{
		if (AbilSys->IsSlotActive(i)) continue;

		FAbilitySlot& Slot = AbilSys->Slots[i];

		// Per-use cooldown tick
		if (Slot.CooldownTimer > 0.f)
		{
			Slot.CooldownTimer = FMath::Max(0.f, Slot.CooldownTimer - DeltaTime);
		}

		// Charge recharge
		if (Slot.RechargeRate <= 0.f) continue;
		if (Slot.Charges < 0) continue;             // -1 = infinite, skip recharge
		if (Slot.Charges >= Slot.MaxCharges) continue;

		Slot.RechargeTimer += DeltaTime;
		while (Slot.RechargeTimer >= Slot.RechargeRate && Slot.Charges < Slot.MaxCharges)
		{
			Slot.Charges++;
			Slot.RechargeTimer -= Slot.RechargeRate;
		}
	}

	// ── Phase 4: Resource pool regeneration ──
	// Always runs. Regen suppressed per-pool if channeled ability is active and !bRegenWhileChanneling.
	{
		FResourcePools* Pools = Entity.try_get_mut<FResourcePools>();
		if (Pools && Pools->PoolCount > 0)
		{
			bool bAnyChanneledActive = false;
			for (int32 i = 0; i < AbilSys->SlotCount; ++i)
			{
				if (AbilSys->IsSlotActive(i) && AbilSys->Slots[i].HasSustainCosts())
				{
					bAnyChanneledActive = true;
					break;
				}
			}
			TickResourceRegen(*Pools, DeltaTime, bAnyChanneledActive);
		}
	}

	// ── Collect results ──
	{
		int32 SlideIdx = AbilSys->FindSlotByType(EAbilityTypeId::Slide);
		if (SlideIdx != INDEX_NONE && AbilSys->IsSlotActive(SlideIdx))
		{
			Results.bSlideActive = true;
			Results.bAnyMovementAbility = true;
		}
	}
	{
		int32 BlinkIdx = AbilSys->FindSlotByType(EAbilityTypeId::Blink);
		if (BlinkIdx != INDEX_NONE)
		{
			FBlinkState* Blink = Entity.try_get_mut<FBlinkState>();
			if (Blink)
			{
				Results.bBlinkAiming = (Blink->State == 2);
				if (Blink->bTeleportedThisFrame)
				{
					Results.bBlinkTeleported = true;
					Results.bAnyMovementAbility = true;
				}
			}
		}
	}
	{
		int32 MantleIdx = AbilSys->FindSlotByType(EAbilityTypeId::Mantle);
		if (MantleIdx != INDEX_NONE && AbilSys->IsSlotActive(MantleIdx))
		{
			Results.bMantling = true;
			Results.bAnyMovementAbility = true;
			FMantleState* Mantle = Entity.try_get_mut<FMantleState>();
			if (Mantle)
			{
				Results.bHanging = (Mantle->Phase == 4);
				Results.MantleType = Mantle->MantleType;
			}
		}
	}

	return Results;
}
