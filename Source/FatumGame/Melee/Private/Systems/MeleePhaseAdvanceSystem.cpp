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
#include "FlecsCharacter.h"   // AFlecsCharacter::PublishMeleeAttackState
#include "FlecsGameTags.h"
#include "FlecsMeleeComponents.h"
#include "FlecsWeaponComponents.h"   // FEquippedBy for reverse lookup

void UFlecsArtillerySubsystem::SetupMeleePhaseAdvanceSystem()
{
	flecs::world& World = *FlecsWorld;

	World.system<FMeleeWeaponInstance, const FMeleeWeaponStatic, const FEquippedBy>("MeleePhaseAdvanceSystem")
		.with<FTagMeleeAttacking>()
		.without<FTagDead>()
		.each([this](flecs::entity Entity,
			FMeleeWeaponInstance& Inst,
			const FMeleeWeaponStatic& Static,
			const FEquippedBy& EquippedBy)
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
					// Blade trail VFX attach is handled on the game thread by
					// AFlecsCharacter::UpdateMeleeBladeTrail — reads the
					// MeleeAttackStatePacked atomic to detect Release entry.
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
					// Blade trail VFX detach is handled on the game thread by
					// AFlecsCharacter::UpdateMeleeBladeTrail — detects Phase leaving Release
					// via MeleeAttackStatePacked atomic.
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

			// Publish the post-transition phase to the owning character's atomic.
			// Consumers: UpdateMeleeProceduralAnim (swing pose), UpdateMeleeBladeTrail
			// (VFX lifecycle), WriteMeleeWeaponBladeSocket (sweep-socket gating).
			if (FCharacterPhysBridge* Bridge = this->FindCharacterBridge(
					Entity.world().entity(static_cast<uint64>(EquippedBy.CharacterEntityId))))
			{
				if (AFlecsCharacter* Actor = Bridge->CharacterActor)
				{
					Actor->PublishMeleeAttackState(Inst.Phase, Inst.ResolvedDirection, Inst.ShapedT);
				}
			}

			UE_LOG(LogTemp, Warning,
				TEXT("[MELEE-DBG] PhaseAdvance: entity=%lld → Phase=%d Timer=%.3f"),
				static_cast<int64>(Entity.id()), (int32)Inst.Phase, Inst.PhaseTimer);
		});
}
