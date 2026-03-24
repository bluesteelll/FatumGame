// FlecsArtillerySubsystem - WeaponTickSystem
// Bloom decay, fire cooldown, burst cooldown, semi-auto reset.

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsWeaponComponents.h"

void UFlecsArtillerySubsystem::SetupWeaponTickSystem()
{
	flecs::world& World = *FlecsWorld;

	// ─────────────────────────────────────────────────────────
	// WEAPON TICK SYSTEM
	// Updates timers for fire rate, burst cooldown, and semi-auto reset.
	// ─────────────────────────────────────────────────────────
	World.system<FWeaponInstance>("WeaponTickSystem")
		.with<FTagWeapon>()
		.without<FTagDead>()
		.each([](flecs::entity Entity, FWeaponInstance& Weapon)
		{
			const float DeltaTime = Entity.world().get_info()->delta_time;

			// Countdown fire cooldown (subtract from clean value, no accumulation error)
			if (Weapon.FireCooldownRemaining > 0.f)
			{
				Weapon.FireCooldownRemaining -= DeltaTime;
			}

			// Update burst cooldown
			if (Weapon.BurstCooldownRemaining > 0.f)
			{
				Weapon.BurstCooldownRemaining -= DeltaTime;
			}

			// Reset semi-auto flag when trigger released
			if (!Weapon.bFireRequested && Weapon.bHasFiredSincePress)
			{
				Weapon.bHasFiredSincePress = false;
			}

			// Bloom decay (CurrentBloom = bloom only, decays to 0)
			const FWeaponStatic* Static = Entity.try_get<FWeaponStatic>();
			if (Static)
			{
				Weapon.TimeSinceLastShot += DeltaTime;
				if (Weapon.TimeSinceLastShot > Static->BloomRecoveryDelay
					&& Weapon.CurrentBloom > 0.f)
				{
					Weapon.CurrentBloom -= Static->BloomDecayRate * DeltaTime;
					if (Weapon.CurrentBloom < 0.f)
					{
						Weapon.CurrentBloom = 0.f;
					}
				}
			}
		});
}
