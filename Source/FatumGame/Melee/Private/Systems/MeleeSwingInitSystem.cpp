// FlecsArtillerySubsystem - MeleeSwingInitSystem (Phase 5)
// Promotes FMeleeChargePayload from Pending → Latched, resolves swing direction,
// checks + consumes stamina, caches phase durations, and enters the Windup phase.
//
// Ownership contract (blueprint §B.2 C2):
//   Pending — cleared by this system after promotion OR cancellation.
//   Latched — written by this system ONLY. MeleeChargeSystem never touches it.
//
// Step order is CRITICAL (blueprint C2 fix): resolve direction → check stamina
// → consume → promote. Any earlier abort MUST NOT consume stamina.
//
// System ordering: ... → MeleeChargeSystem → MeleeSwingInitSystem →
//                  MeleePhaseAdvanceSystem → MeleeSweepSystem → ...

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsMeleeComponents.h"
#include "FlecsWeaponComponents.h"             // FEquippedBy
#include "Components/FlecsResourceTypes.h"     // FResourcePools, EResourceTypeId

namespace
{
	/** Classify "resolution failed" — buffer or character absent. Buffer always returns a
	 *  valid enum value, so we only treat total absence as failure (blueprint §B.2 step 2). */
	constexpr float kSwingDirectionWindowSecondsPlaceholder = 0.13f;

	/** Clear Pending + swing-start state with NO stamina consumed (early-abort path). */
	void CancelPendingNoStamina(FMeleeWeaponInstance& Inst)
	{
		Inst.PendingPayload = FMeleeChargePayload{};
	}
}

void UFlecsArtillerySubsystem::SetupMeleeSwingInitSystem()
{
	flecs::world& World = *FlecsWorld;

	World.system<FMeleeWeaponInstance, const FMeleeWeaponStatic, const FEquippedBy>("MeleeSwingInitSystem")
		.with<FTagMeleeWeapon>()
		.without<FTagDead>()
		.each([&World](flecs::entity Entity,
			FMeleeWeaponInstance& Inst,
			const FMeleeWeaponStatic& Static,
			const FEquippedBy& EquippedBy)
		{
			// ── Gate 0: only run on idle weapons with a pending payload. ──
			if (Inst.Phase != EMeleeAttackPhase::Idle) return;
			if (!Inst.PendingPayload.bValid) return;
			if (!EquippedBy.IsEquipped()) return;

			// ── Step 1: resolve character entity. ──
			// Required for direction buffer + stamina pool lookup.
			flecs::entity Character = World.entity(static_cast<uint64>(EquippedBy.CharacterEntityId));
			if (!Character.is_valid() || !Character.is_alive())
			{
				// Stale equip reference — abort without consuming stamina.
				UE_LOG(LogTemp, Verbose,
					TEXT("MeleeSwingInit: character entity %lld invalid; cancelling pending"),
					EquippedBy.CharacterEntityId);
				CancelPendingNoStamina(Inst);
				return;
			}

			// ── Step 2: RESOLVE DIRECTION FIRST (blueprint C2 order). ──
			EMeleeSwingDirection Resolved = EMeleeSwingDirection::Horizontal;
			if (const FMeleeAttackDirectionBuffer* DirBuf = Character.try_get<FMeleeAttackDirectionBuffer>())
			{
				Resolved = DirBuf->Resolve(kSwingDirectionWindowSecondsPlaceholder);
			}
			else
			{
				// No buffer yet — legitimate early-startup state. Fall back to Pending direction
				// (which was seeded by MeleeChargeSystem at emit). Do NOT abort — this is an
				// expected transient on freshly-spawned characters (blueprint: Phase 3 ensures
				// character has buffer on melee equip).
				Resolved = Inst.PendingPayload.Direction;
			}

			// ── Step 3: stamina check (no consumption yet). ──
			const float StaminaCost = Static.StaminaCostPerSwing * Inst.PendingPayload.StaminaCostScale;

			FResourcePools* Pools = Character.try_get_mut<FResourcePools>();
			bool bHasStaminaPool = false;
			if (Pools)
			{
				const int32 StamIdx = Pools->FindPool(EResourceTypeId::Stamina);
				if (StamIdx != INDEX_NONE)
				{
					bHasStaminaPool = true;
					if (Pools->Pools[StamIdx].CurrentValue < StaminaCost)
					{
						// Insufficient stamina — cancel pending; NO consumption (blueprint C2).
						CancelPendingNoStamina(Inst);
						return;
					}
				}
			}

			// MVP tolerance: characters without a stamina pool swing for free. Log so we notice.
			if (!bHasStaminaPool)
			{
				UE_LOG(LogTemp, Verbose,
					TEXT("MeleeSwingInit: character %lld has no Stamina pool — allowing free swing (MVP)"),
					EquippedBy.CharacterEntityId);
			}

			// ── Step 4: consume stamina. ──
			if (bHasStaminaPool)
			{
				Pools->Consume(EResourceTypeId::Stamina, StaminaCost);
			}

			// ── Step 5: promote Pending → Latched. ──
			Inst.LatchedPayload = Inst.PendingPayload;
			Inst.PendingPayload = FMeleeChargePayload{};

			// ── Step 6: cache durations scaled by SwingSpeedMul. ──
			// Guard divisor against degenerate multipliers (charge shaping clamps to [1, MaxMult],
			// but be defensive — Static.FromProfile validates inputs, not the runtime payload).
			const float SpeedMul = FMath::Max(Inst.LatchedPayload.SwingSpeedMul, 0.1f);
			Inst.WindupDuration   = Static.WindupTime   / SpeedMul;
			Inst.ReleaseDuration  = Static.ReleaseTime  / SpeedMul;
			Inst.RecoveryDuration = Static.RecoveryTime / SpeedMul;

			// ── Step 7: reset per-swing state. ──
			// B3 fix: scale RemainingBudget by Latched PenetrationMul so charged swings
			// actually get more pen budget per blueprint §D promise.
			Inst.PenState.RemainingBudget         = Static.MeleePen.PenetrationBudget * Inst.LatchedPayload.PenetrationMul;
			Inst.PenState.CurrentDamageMultiplier = 1.f;
			Inst.PenState.PenetrationCount        = 0;
			Inst.PenState.LastPenetratedTargetId  = 0;

			for (int32 i = 0; i < FMeleeWeaponInstance::MaxHitsPerSwing; ++i)
			{
				Inst.HitTargetIds[i] = 0;
			}
			Inst.HitCount            = 0;
			Inst.bSwingRebounded     = false;
			Inst.LastSweepFrameStamp = 0;   // forces stale-handling on first Release tick (C5)
			Inst.LastTipSpeed        = 0.f;

			// ── Step 8: enter Windup + tag for sweep/phase-advance queries. ──
			Inst.Phase             = EMeleeAttackPhase::Windup;
			Inst.PhaseTimer        = Inst.WindupDuration;
			Inst.ResolvedDirection = Resolved;
			Inst.ShapedT           = Inst.LatchedPayload.ShapedT;
			Entity.add<FTagMeleeAttacking>();

			// ── Step 9: publish atomic to game thread. ──
			// TODO(Phase 5+ followup): reverse lookup flecs::entity → AFlecsCharacter* is not
			// present in the codebase. Without it, we cannot call PublishMeleeAttackState()
			// from sim. Game thread will fall back to always-on socket sampling in
			// WriteMeleeWeaponBladeSocket (acceptable per Phase 5 scope). Wire this once a
			// clean reverse map (e.g. FlecsCharacterActorRegistry) lands.

			UE_LOG(LogTemp, Verbose,
				TEXT("MeleeSwingInit: entity=%lld Windup=%.3f Release=%.3f Recovery=%.3f Dir=%d ShapedT=%.3f"),
				static_cast<int64>(Entity.id()),
				Inst.WindupDuration, Inst.ReleaseDuration, Inst.RecoveryDuration,
				static_cast<int32>(Resolved), Inst.ShapedT);
		});
}
