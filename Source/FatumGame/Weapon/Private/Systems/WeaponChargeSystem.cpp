// FlecsArtillerySubsystem - WeaponChargeSystem
// Hold-to-charge accumulator for charged shots.
//
// Ownership contract (NEVER violate):
//   PendingPayload  — owned by WeaponChargeSystem. Writes on emit, clears on cancel.
//                     WeaponFireSystem reads once at tick-top (Pending→Latched promotion) then clears.
//   LatchedPayload  — owned by WeaponFireSystem. Read here, never written here.
//
// System ordering: WeaponEquipSystem → WeaponChargeSystem → WeaponTickSystem
//                  → WeaponReloadSystem → WeaponFireSystem
//
// Charge runs BEFORE tick (so fresh CooldownRemaining isn't a barrier this tick — Step 4 uses CanFire())
// and BEFORE reload/fire so an emitted Pending is promoted to Latched in the SAME StepWorld.

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsWeaponComponents.h"
#include "Curves/CurveFloat.h"

namespace
{
	/** Cancel charge accumulation. Clears Pending (owner: this system), DOES NOT touch Latched. */
	void CancelCharge(flecs::entity Entity, FWeaponInstance& Weapon)
	{
		Weapon.bIsCharging = false;
		Weapon.ChargeAccumulator = 0.f;
		if (Entity.has<FTagChargingWeapon>())
			Entity.remove<FTagChargingWeapon>();
		Weapon.PendingPayload = FChargeShotPayload{};
	}

	/** Build a payload from raw t and store in Pending slot. Asserts Pending was empty. */
	void EmitChargedShot(flecs::entity Entity, FWeaponInstance& Weapon, const FWeaponStatic* Static, float RawT)
	{
		checkf(!Weapon.PendingPayload.bValid,
			TEXT("EmitChargedShot: Pending slot already occupied — ownership violation (entity=%lld)"),
			static_cast<int64>(Entity.id()));

		const float ShapedT = Static->ChargeCurve
			? FMath::Clamp(Static->ChargeCurve->GetFloatValue(RawT), 0.f, 1.f)
			: FMath::Clamp(RawT, 0.f, 1.f);

		FChargeShotPayload P;
		P.bValid = true;
		P.ShapedT = ShapedT;
		P.DamageMul          = FMath::Lerp(1.f, Static->DamageMaxMultiplier,          ShapedT);
		P.ProjectileSpeedMul = FMath::Lerp(1.f, Static->ProjectileSpeedMaxMultiplier, ShapedT);
		P.PenetrationMul     = FMath::Lerp(1.f, Static->PenetrationMaxMultiplier,     ShapedT);
		P.SpreadMul          = FMath::Lerp(1.f, Static->SpreadMaxMultiplier,          ShapedT);
		P.BloomMul           = FMath::Lerp(1.f, Static->BloomMaxMultiplier,           ShapedT);
		P.RecoilMul          = FMath::Lerp(1.f, Static->RecoilMaxMultiplier,          ShapedT);
		P.ImpulseMul         = FMath::Lerp(1.f, Static->ImpulseMaxMultiplier,         ShapedT);

		if (Static->ChargeAmmoMode == 1 /* ScalesWithCharge */)
		{
			const int32 Lo = FMath::Max(1, Static->AmmoPerShot);
			const int32 Hi = FMath::Max(Lo, Static->MaxAmmoAtFullCharge);
			P.AmmoCount = FMath::RoundToInt(FMath::Lerp(static_cast<float>(Lo), static_cast<float>(Hi), ShapedT));
			P.AmmoCount = FMath::Max(1, P.AmmoCount);
		}
		else
		{
			P.AmmoCount = FMath::Max(1, Static->AmmoPerShot);
		}

		Weapon.PendingPayload = P;
		Weapon.bIsCharging = false;
		Weapon.ChargeAccumulator = 0.f;
		if (Entity.has<FTagChargingWeapon>())
			Entity.remove<FTagChargingWeapon>();

		UE_LOG(LogTemp, Verbose,
			TEXT("CHARGE emit: entity=%lld rawT=%.3f shapedT=%.3f Ammo=%d Dmg×=%.2f Spd×=%.2f Pen×=%.2f Spr×=%.2f Blm×=%.2f"),
			static_cast<int64>(Entity.id()), RawT, ShapedT, P.AmmoCount,
			P.DamageMul, P.ProjectileSpeedMul, P.PenetrationMul, P.SpreadMul, P.BloomMul);
	}
}

void UFlecsArtillerySubsystem::SetupWeaponChargeSystem()
{
	flecs::world& World = *FlecsWorld;

	World.system<FWeaponInstance, const FEquippedBy>("WeaponChargeSystem")
		.with<FTagWeapon>()
		.without<FTagDead>()
		.each([](flecs::entity Entity, FWeaponInstance& Weapon, const FEquippedBy& EquippedBy)
		{
			if (!EquippedBy.IsEquipped()) return;

			const FWeaponStatic* Static = Entity.try_get<FWeaponStatic>();
			if (!Static) return;

			const float DeltaTime = Entity.world().get_info()->delta_time;

			// ── STEP 1: gate by bEnableCharge ──
			if (!Static->bEnableCharge)
			{
				if (Entity.has<FTagChargingWeapon>())
					Entity.remove<FTagChargingWeapon>();
				Weapon.bIsCharging = false;
				Weapon.ChargeAccumulator = 0.f;
				Weapon.bWasFireRequestedLastTick = Weapon.bFireRequested;
				return;
			}

			// ── STEP 2: reload/cycle cancels charge ──
			if (Weapon.IsReloading() || Weapon.bCycling || Weapon.ReloadPhase != EWeaponReloadPhase::Idle)
			{
				CancelCharge(Entity, Weapon);
				Weapon.bWasFireRequestedLastTick = Weapon.bFireRequested;
				return;
			}

			// ── STEP 3: consume bPendingAutoRestart once user releases ──
			if (Weapon.bPendingAutoRestart && !Weapon.bFireRequested)
			{
				Weapon.bPendingAutoRestart = false;
			}

			// ── STEP 4: start new charge on press edge (all conditions clean) ──
			if (Weapon.bFireRequested
				&& !Weapon.bIsCharging
				&& !Weapon.bPendingAutoRestart
				&& !Weapon.LatchedPayload.bValid
				&& !Weapon.PendingPayload.bValid
				&& Weapon.CanFire())
			{
				Weapon.bIsCharging = true;
				Weapon.ChargeAccumulator = 0.f;
				Entity.add<FTagChargingWeapon>();
			}

			// ── STEP 5: accumulate; auto-fire-at-max ──
			if (Weapon.bIsCharging)
			{
				Weapon.ChargeAccumulator = FMath::Min(Weapon.ChargeAccumulator + DeltaTime, Static->MaxChargeTime);

				if (Static->bAutoFireAtMaxCharge && Weapon.ChargeAccumulator >= Static->MaxChargeTime)
				{
					EmitChargedShot(Entity, Weapon, Static, 1.0f);
					Weapon.bPendingAutoRestart = Static->bAutoRestartCharge;
				}
			}

			// ── STEP 6: falling-edge release detection ──
			if (Weapon.bWasFireRequestedLastTick && !Weapon.bFireRequested && Weapon.bIsCharging)
			{
				const float RawT = Weapon.ChargeAccumulator / Static->MaxChargeTime;
				if (Weapon.ChargeAccumulator < Static->MinChargeTime)
				{
					CancelCharge(Entity, Weapon);  // released too early — no shot, no ammo
				}
				else
				{
					EmitChargedShot(Entity, Weapon, Static, RawT);
				}
			}

			// ── STEP 7: snapshot for next-tick edge detection ──
			Weapon.bWasFireRequestedLastTick = Weapon.bFireRequested;
		});
}
