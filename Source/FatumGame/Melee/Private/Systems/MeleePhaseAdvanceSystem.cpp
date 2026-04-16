// FlecsArtillerySubsystem - MeleePhaseAdvanceSystem (Phase 5)
// Advances the swing phase machine (Windup → Release → Recovery → Idle) on
// zero-crossings of PhaseTimer. Pure state transitions — no physics queries.
//
// System ordering: ... → MeleeSwingInitSystem → MeleePhaseAdvanceSystem →
//                  MeleeSweepSystem → ...
//
// Note: MeleeSweepSystem branches on Phase == Release within its own query, so
// this system's transitions take effect *next* sim tick for sweep logic. That's
// the intended behaviour — the tick that promotes Release is also the first
// sweep tick (PhaseTimer decrements AFTER this system runs on the following tick).

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsMeleeComponents.h"

void UFlecsArtillerySubsystem::SetupMeleePhaseAdvanceSystem()
{
	flecs::world& World = *FlecsWorld;

	World.system<FMeleeWeaponInstance, const FMeleeWeaponStatic>("MeleePhaseAdvanceSystem")
		.with<FTagMeleeAttacking>()
		.without<FTagDead>()
		.each([](flecs::entity Entity, FMeleeWeaponInstance& Inst, const FMeleeWeaponStatic& Static)
		{
			const float DeltaTime = Entity.world().get_info()->delta_time;

			Inst.PhaseTimer -= DeltaTime;
			if (Inst.PhaseTimer > 0.f) return;

			// ── Zero-crossing transition ──
			switch (Inst.Phase)
			{
				case EMeleeAttackPhase::Windup:
				{
					Inst.Phase      = EMeleeAttackPhase::Release;
					Inst.PhaseTimer = Inst.ReleaseDuration;
					// TODO(Phase 7): NiagaraMgr->EnqueueBladeTrail(...) on Release entry.
					// TODO(Phase 5+ followup): publish MeleeAttackStatePacked atomic — requires
					// entity→actor reverse lookup (see MeleeSwingInitSystem for notes).
					break;
				}

				case EMeleeAttackPhase::Release:
				{
					Inst.Phase      = EMeleeAttackPhase::Recovery;
					Inst.PhaseTimer = Inst.RecoveryDuration;

					// Rebound penalty: Slashing hitting Metal/Armor in this swing extends recovery.
					// One-shot flag set in MeleeSweepSystem; multiplier defaults to 1.0 (no-op).
					if (Inst.bSwingRebounded && Static.ReboundRecoveryMultiplier > 1.f)
					{
						Inst.PhaseTimer += (Static.ReboundRecoveryMultiplier - 1.f) * Inst.RecoveryDuration;
					}
					// TODO(Phase 7): NiagaraMgr->DequeueBladeTrailDetach(...) on Recovery entry.
					break;
				}

				case EMeleeAttackPhase::Recovery:
				{
					Inst.Phase      = EMeleeAttackPhase::Idle;
					Inst.PhaseTimer = 0.f;
					Inst.LatchedPayload = FMeleeChargePayload{};
					if (Entity.has<FTagMeleeAttacking>())
					{
						Entity.remove<FTagMeleeAttacking>();
					}
					// TODO(Phase 5+ followup): publish MeleeAttackStatePacked atomic with Idle.
					break;
				}

				default:
					// Idle/Charging with FTagMeleeAttacking would be an invariant violation —
					// MeleeSwingInitSystem is the only writer that adds the tag, and it only
					// adds it when entering Windup. If we see it, log once and drop the tag.
					UE_LOG(LogTemp, Error,
						TEXT("MeleePhaseAdvance: entity %lld has FTagMeleeAttacking but Phase=%d (invariant violated)"),
						static_cast<int64>(Entity.id()), static_cast<int32>(Inst.Phase));
					if (Entity.has<FTagMeleeAttacking>())
					{
						Entity.remove<FTagMeleeAttacking>();
					}
					break;
			}
		});
}
