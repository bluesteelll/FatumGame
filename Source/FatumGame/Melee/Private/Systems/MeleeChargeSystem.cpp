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
#include "FlecsGameTags.h"
#include "FlecsMeleeComponents.h"
#include "FlecsWeaponComponents.h" // FEquippedBy, FTagWeapon
#include "Curves/CurveFloat.h"

namespace
{
	/** Cancel in-progress charge. Clears Pending (owner: this system), DOES NOT touch Latched. */
	void CancelMeleeCharge(flecs::entity Entity, FMeleeWeaponInstance& Inst)
	{
		Inst.bIsCharging = false;
		Inst.ChargeAccumulator = 0.f;
		Inst.bPendingAutoRestart = false;
		if (Entity.has<FTagMeleeCharging>())
			Entity.remove<FTagMeleeCharging>();
		Inst.PendingPayload = FMeleeChargePayload{};
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
		.each([](flecs::entity Entity, FMeleeWeaponInstance& Inst, const FEquippedBy& EquippedBy)
		{
			if (!EquippedBy.IsEquipped()) return;

			const FMeleeWeaponStatic* Static = Entity.try_get<FMeleeWeaponStatic>();
			if (!Static) return;

			const float DeltaTime = Entity.world().get_info()->delta_time;

			// ── STEP 1: mid-swing cancels charge unconditionally ──
			// Distinct from ranged: melee has no cycle phase, but any non-Idle phase
			// (Windup/Release/Recovery) blocks a new charge until Idle returns.
			if (Inst.Phase != EMeleeAttackPhase::Idle)
			{
				CancelMeleeCharge(Entity, Inst);
				Inst.bWasAttackRequestedLastTick = Inst.bAttackRequested;
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
				Entity.add<FTagMeleeCharging>();
			}

			// ── STEP 5: accumulate; auto-fire-at-max ──
			if (Inst.bIsCharging)
			{
				Inst.ChargeAccumulator = FMath::Min(Inst.ChargeAccumulator + DeltaTime, Static->MaxChargeTime);

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
					CancelMeleeCharge(Entity, Inst);  // released too early — no swing
				}
				else
				{
					const float RawT = FMath::Clamp(Inst.ChargeAccumulator / Static->MaxChargeTime, 0.f, 1.f);
					EmitMeleeChargedSwing(Entity, Inst, Static, RawT);
				}
			}

			// ── STEP 7: snapshot for next-tick edge detection ──
			Inst.bWasAttackRequestedLastTick = Inst.bAttackRequested;
		});
}
