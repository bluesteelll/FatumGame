// FlecsArtillerySubsystem - MeleeChargeSystem
// Hold-to-charge accumulator for melee charged swings. Mirrors WeaponChargeSystem
// (ranged) but tracks bAttackRequested instead of bFireRequested and emits a
// FMeleeChargePayload on FMeleeWeaponInstance.PendingPayload.
//
// Ownership contract (NEVER violate — mirrors ranged):
//   PendingPayload  — owned by MeleeChargeSystem. Writes on emit, clears on cancel.
//                     MeleeSwingInitSystem (Phase 5) will read and promote Pending→Latched.
//   LatchedPayload  — owned by MeleeSwingInitSystem (Phase 5). NEVER written here.
//
// System ordering: WeaponEquipSystem → WeaponChargeSystem → WeaponTickSystem →
//                  WeaponReloadSystem → WeaponFireSystem → MeleeChargeSystem → (Phase 5+)
//
// Mid-swing cancel: if Phase != Idle the charge is cancelled immediately.
// A fresh press during Recovery will therefore be ignored until Idle returns,
// matching the ranged "no-fire-during-reload" semantics.

#include "FlecsArtillerySubsystem.h"
#include "FlecsCharacter.h"         // PublishMeleeAttackState
#include "FlecsGameTags.h"
#include "FlecsMeleeComponents.h"
#include "FlecsWeaponComponents.h" // FEquippedBy, FTagWeapon
#include "Curves/CurveFloat.h"

namespace
{
	/** Cancel in-progress charge. Clears Pending (owner: this system), DOES NOT touch Latched.
	 *  Sets Phase=Idle so the procedural anim / atomic publisher return to rest pose. */
	void CancelMeleeCharge(flecs::entity Entity, FMeleeWeaponInstance& Inst)
	{
		Inst.bIsCharging = false;
		Inst.ChargeAccumulator = 0.f;
		Inst.bPendingAutoRestart = false;
		if (Entity.has<FTagMeleeCharging>())
			Entity.remove<FTagMeleeCharging>();
		Inst.PendingPayload = FMeleeChargePayload{};
		// Only clear Phase if it was in Charging — don't stomp Windup/Release/Recovery.
		if (Inst.Phase == EMeleeAttackPhase::Charging)
		{
			Inst.Phase   = EMeleeAttackPhase::Idle;
			Inst.ShapedT = 0.f;
		}
	}

	/** Build a payload from raw t and store in Pending slot. Asserts Pending was empty. */
	void EmitMeleeChargedSwing(flecs::entity Entity, FMeleeWeaponInstance& Inst, const FMeleeWeaponStatic* Static, float RawT)
	{
		checkf(!Inst.PendingPayload.bValid,
			TEXT("EmitMeleeChargedSwing: Pending slot already occupied — ownership violation (entity=%lld)"),
			static_cast<int64>(Entity.id()));

		const float ShapedT = Static->ChargeCurve
			? FMath::Clamp(Static->ChargeCurve->GetFloatValue(RawT), 0.f, 1.f)
			: FMath::Clamp(RawT, 0.f, 1.f);

		FMeleeChargePayload P;
		P.bValid           = true;
		P.ShapedT          = ShapedT;
		P.DamageMul        = FMath::Lerp(1.f, Static->DamageMaxMultiplier,        ShapedT);
		P.ImpulseMul       = FMath::Lerp(1.f, Static->ImpulseMaxMultiplier,       ShapedT);
		P.PenetrationMul   = FMath::Lerp(1.f, Static->PenetrationMaxMultiplier,   ShapedT);
		P.SwingSpeedMul    = FMath::Lerp(1.f, Static->SwingSpeedMaxMultiplier,    ShapedT);
		P.StaminaCostScale = FMath::Lerp(1.f, Static->StaminaCostMaxMultiplier,   ShapedT);
		// Direction is a placeholder — MeleeSwingInitSystem (Phase 5) resolves it from
		// FMeleeAttackDirectionBuffer at the moment of promotion.
		P.Direction        = EMeleeSwingDirection::Horizontal;

		Inst.PendingPayload = P;
		Inst.bIsCharging = false;
		Inst.ChargeAccumulator = 0.f;
		if (Entity.has<FTagMeleeCharging>())
			Entity.remove<FTagMeleeCharging>();

		UE_LOG(LogTemp, Verbose,
			TEXT("MELEE CHARGE emit: entity=%lld rawT=%.3f shapedT=%.3f Dmg×=%.2f Imp×=%.2f Pen×=%.2f Spd×=%.2f Sta×=%.2f"),
			static_cast<int64>(Entity.id()), RawT, ShapedT,
			P.DamageMul, P.ImpulseMul, P.PenetrationMul, P.SwingSpeedMul, P.StaminaCostScale);
	}
}

void UFlecsArtillerySubsystem::SetupMeleeChargeSystem()
{
	flecs::world& World = *FlecsWorld;

	World.system<FMeleeWeaponInstance, const FEquippedBy>("MeleeChargeSystem")
		.with<FTagMeleeWeapon>()
		.without<FTagDead>()
		.each([this](flecs::entity Entity, FMeleeWeaponInstance& Inst, const FEquippedBy& EquippedBy)
		{
			if (!EquippedBy.IsEquipped())
			{
				// Log on rising edge only (attack pressed on unequipped weapon shouldn't happen,
				// but if it does we want to see it — otherwise silently ignore to prevent spam).
				if (Inst.bAttackRequested && !Inst.bWasAttackRequestedLastTick)
				{
					UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] ChargeSystem: entity=%lld bAttackRequested=1 but NOT EQUIPPED (CharacterEntityId=0) → ignored"),
						static_cast<int64>(Entity.id()));
				}
				return;
			}

			const FMeleeWeaponStatic* Static = Entity.try_get<FMeleeWeaponStatic>();
			if (!Static)
			{
				if (Inst.bAttackRequested && !Inst.bWasAttackRequestedLastTick)
				{
					UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] ChargeSystem: entity=%lld has NO FMeleeWeaponStatic → ignored"),
						static_cast<int64>(Entity.id()));
				}
				return;
			}

			// Edge-trigger diagnostic — fires once when LMB goes from up→down or down→up
			const bool bRisingEdge  =  Inst.bAttackRequested && !Inst.bWasAttackRequestedLastTick;
			const bool bFallingEdge = !Inst.bAttackRequested &&  Inst.bWasAttackRequestedLastTick;
			if (bRisingEdge || bFallingEdge)
			{
				UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] ChargeSystem ENTRY: entity=%lld edge=%s Phase=%d bIsCharging=%d ChargeAccum=%.3f Pending=%d Latched=%d bEnableCharge=%d"),
					static_cast<int64>(Entity.id()),
					bRisingEdge ? TEXT("PRESS") : TEXT("RELEASE"),
					(int32)Inst.Phase, Inst.bIsCharging ? 1 : 0, Inst.ChargeAccumulator,
					Inst.PendingPayload.bValid ? 1 : 0, Inst.LatchedPayload.bValid ? 1 : 0,
					Static->bEnableCharge ? 1 : 0);
			}

			const float DeltaTime = Entity.world().get_info()->delta_time;

			// ── STEP 1: mid-SWING cancels charge. ──
			// Distinct from ranged: melee has no cycle phase, but Windup/Release/Recovery
			// are post-commit strike phases — a new press during them must wait until Idle.
			// Charging itself is NOT a cancel trigger (that would immediately wipe the phase
			// we just entered on the previous press tick — visible as a single-frame twitch).
			if (Inst.Phase == EMeleeAttackPhase::Windup
			 || Inst.Phase == EMeleeAttackPhase::Release
			 || Inst.Phase == EMeleeAttackPhase::Recovery)
			{
				if (bRisingEdge)
				{
					UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] ChargeSystem: PRESS blocked by Phase=%d (mid-swing) → charge CANCELLED"),
						(int32)Inst.Phase);
				}
				CancelMeleeCharge(Entity, Inst);
				Inst.bWasAttackRequestedLastTick = Inst.bAttackRequested;
				Inst.bWasBlockRequestedLastTick  = Inst.bBlockRequested;
				return;
			}

			// ── STEP 2: instant-fire path when charge is disabled ──
			if (!Static->bEnableCharge)
			{
				if (Entity.has<FTagMeleeCharging>())
					Entity.remove<FTagMeleeCharging>();
				Inst.bIsCharging = false;
				Inst.ChargeAccumulator = 0.f;
				Inst.bPendingAutoRestart = false;

				// Rising-edge press → emit unit payload immediately.
				if (Inst.bAttackRequested && !Inst.bWasAttackRequestedLastTick && !Inst.PendingPayload.bValid)
				{
					FMeleeChargePayload P;
					P.bValid           = true;
					P.ShapedT          = 1.f;
					P.DamageMul        = 1.f;
					P.ImpulseMul       = 1.f;
					P.PenetrationMul   = 1.f;
					P.SwingSpeedMul    = 1.f;
					P.StaminaCostScale = 1.f;
					P.Direction        = EMeleeSwingDirection::Horizontal;
					Inst.PendingPayload = P;
				}
				Inst.bWasAttackRequestedLastTick = Inst.bAttackRequested;
				Inst.bWasBlockRequestedLastTick  = Inst.bBlockRequested;
				return;
			}

			// ── STEP 3: consume bPendingAutoRestart once user releases ──
			if (Inst.bPendingAutoRestart && !Inst.bAttackRequested)
			{
				Inst.bPendingAutoRestart = false;
			}

			// ── STEP 4: start new charge on press edge ──
			if (Inst.bAttackRequested
				&& !Inst.bWasAttackRequestedLastTick
				&& !Inst.bIsCharging
				&& !Inst.bPendingAutoRestart
				&& !Inst.PendingPayload.bValid
				&& !Inst.LatchedPayload.bValid)
			{
				Inst.bIsCharging = true;
				Inst.ChargeAccumulator = 0.f;
				Inst.Phase             = EMeleeAttackPhase::Charging;
				Inst.ShapedT           = 0.f;
				Entity.add<FTagMeleeCharging>();

				// Reset direction accumulator on charge start — only motion DURING the hold
				// counts toward direction classification (no stale pre-press mouse motion).
				flecs::entity CharE = Entity.world().entity(
					static_cast<uint64>(EquippedBy.CharacterEntityId));
				if (CharE.is_valid() && CharE.is_alive())
				{
					if (FMeleeAttackDirectionBuffer* Buf = CharE.try_get_mut<FMeleeAttackDirectionBuffer>())
					{
						Buf->Reset();
					}
				}

				UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] ChargeSystem: STEP 4 → charge STARTED (entity=%lld)"),
					static_cast<int64>(Entity.id()));
			}
			else if (bRisingEdge)
			{
				UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] ChargeSystem: STEP 4 BLOCKED (bIsCharging=%d bPendingAutoRestart=%d Pending=%d Latched=%d)"),
					Inst.bIsCharging ? 1 : 0, Inst.bPendingAutoRestart ? 1 : 0,
					Inst.PendingPayload.bValid ? 1 : 0, Inst.LatchedPayload.bValid ? 1 : 0);
			}

			// ── STEP 5: accumulate; auto-fire-at-max ──
			if (Inst.bIsCharging)
			{
				Inst.ChargeAccumulator = FMath::Min(Inst.ChargeAccumulator + DeltaTime, Static->MaxChargeTime);

				// Update ShapedT live so UpdateMeleeProceduralAnim can scale the cock-back
				// pose proportionally during Charging (Mordhau-style "hold = wind up visual").
				// Apply the charge curve when available so the visual matches the damage curve.
				const float RawT = Inst.ChargeAccumulator / FMath::Max(Static->MaxChargeTime, KINDA_SMALL_NUMBER);
				Inst.ShapedT = Static->ChargeCurve
					? FMath::Clamp(Static->ChargeCurve->GetFloatValue(RawT), 0.f, 1.f)
					: FMath::Clamp(RawT, 0.f, 1.f);

				if (Static->bAutoFireAtMaxCharge
					&& Inst.ChargeAccumulator >= Static->MaxChargeTime
					&& !Inst.PendingPayload.bValid)
				{
					EmitMeleeChargedSwing(Entity, Inst, Static, 1.0f);
					if (Static->bAutoRestartCharge)
					{
						// Cycle: keep charging, restart accumulator. bPendingAutoRestart gates
						// are not used in the cycling path — the release edge below will end it.
						Inst.bIsCharging = true;
						Inst.ChargeAccumulator = 0.f;
						Entity.add<FTagMeleeCharging>();
					}
					else
					{
						// Auto-fire once; block re-charge until user releases then re-presses.
						Inst.bPendingAutoRestart = true;
					}
				}
			}

			// ── STEP 6: falling-edge release detection ──
			if (Inst.bWasAttackRequestedLastTick && !Inst.bAttackRequested && Inst.bIsCharging)
			{
				if (Inst.ChargeAccumulator < Static->MinChargeTime)
				{
					UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] ChargeSystem: STEP 6 RELEASE too early (Accum=%.3f < Min=%.3f) → CANCEL"),
						Inst.ChargeAccumulator, Static->MinChargeTime);
					CancelMeleeCharge(Entity, Inst);  // released too early — no swing
				}
				else
				{
					const float RawT = FMath::Clamp(Inst.ChargeAccumulator / Static->MaxChargeTime, 0.f, 1.f);
					UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] ChargeSystem: STEP 6 RELEASE valid (Accum=%.3f rawT=%.3f) → EMIT Pending"),
						Inst.ChargeAccumulator, RawT);
					EmitMeleeChargedSwing(Entity, Inst, Static, RawT);
				}
			}
			else if (bFallingEdge)
			{
				UE_LOG(LogTemp, Warning, TEXT("[MELEE-DBG] ChargeSystem: STEP 6 release but bIsCharging=0 → ignored"));
			}

			// ── STEP 7: snapshot for next-tick edge detection ──
			Inst.bWasAttackRequestedLastTick = Inst.bAttackRequested;

			// ── STEP 8: block state edge detection (Phase 6 — §B.2 BlockAbsorbSystem) ──
			// FTagMeleeBlocking lives on the CHARACTER entity, not the weapon.
			// bIsBlocking + BlockStartTimestampSim live on the weapon instance.
			if (Static->bCanBlock)
			{
				const bool bBlockPressed  = Inst.bBlockRequested && !Inst.bWasBlockRequestedLastTick;
				const bool bBlockReleased = !Inst.bBlockRequested && Inst.bWasBlockRequestedLastTick;

				if (bBlockPressed && !Inst.bIsBlocking && Inst.Phase == EMeleeAttackPhase::Idle)
				{
					Inst.bIsBlocking = true;
					// TODO(N-M2): use FSimulationWorker wall-clock RealDT accumulator for
					// time-dilation-immune perfect-block timing. Flecs world.time() is acceptable
					// for single-player MVP without heavy dilation.
					Inst.BlockStartTimestampSim = Entity.world().get_info()->world_time_total;

					flecs::entity CharEntity = Entity.world().entity(
						static_cast<uint64>(EquippedBy.CharacterEntityId));
					if (CharEntity.is_valid() && CharEntity.is_alive())
					{
						CharEntity.add<FTagMeleeBlocking>();
					}
				}
				else if (bBlockReleased && Inst.bIsBlocking)
				{
					Inst.bIsBlocking = false;

					flecs::entity CharEntity = Entity.world().entity(
						static_cast<uint64>(EquippedBy.CharacterEntityId));
					if (CharEntity.is_valid() && CharEntity.is_alive())
					{
						CharEntity.remove<FTagMeleeBlocking>();
					}
				}

				// Cancel block when a swing starts (Phase != Idle is caught by Step 1 early-return,
				// so this handles the edge case of attack-and-block on the same tick).
				if (Inst.bIsBlocking && Inst.PendingPayload.bValid)
				{
					Inst.bIsBlocking = false;

					flecs::entity CharEntity = Entity.world().entity(
						static_cast<uint64>(EquippedBy.CharacterEntityId));
					if (CharEntity.is_valid() && CharEntity.is_alive())
					{
						CharEntity.remove<FTagMeleeBlocking>();
					}
				}
			}

			Inst.bWasBlockRequestedLastTick = Inst.bBlockRequested;

			// ── STEP 9: publish atomic for Charging/Idle transitions. ──
			// Windup/Release/Recovery publishes are owned by SwingInit / PhaseAdvance.
			// This publish lets UpdateMeleeProceduralAnim scale the cock-back pose
			// proportional to ShapedT while LMB is held (Mordhau-style windup visual).
			if (Inst.Phase == EMeleeAttackPhase::Charging || Inst.Phase == EMeleeAttackPhase::Idle)
			{
				flecs::entity CharE = Entity.world().entity(
					static_cast<uint64>(EquippedBy.CharacterEntityId));
				if (FCharacterPhysBridge* Bridge = this->FindCharacterBridge(CharE))
				{
					if (AFlecsCharacter* Actor = Bridge->CharacterActor)
					{
						Actor->PublishMeleeAttackState(Inst.Phase, EMeleeSwingDirection::Horizontal, Inst.ShapedT);
					}
				}
			}
		});
}
