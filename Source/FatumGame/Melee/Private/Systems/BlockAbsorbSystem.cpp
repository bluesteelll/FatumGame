// FlecsArtillerySubsystem - BlockAbsorbSystem (Phase 6)
// Drains FPendingBlockAbsorb accumulated by MeleeSweepSystem this tick.
// Consumes stamina from the defender's resource pools. On stamina depletion,
// breaks the defender's guard (removes FTagMeleeBlocking, clears weapon bIsBlocking).
//
// System ordering: ... → MeleeSweepSystem → BlockAbsorbSystem → (vitals regen) → ...
// Blueprint reference: §B.2 BlockAbsorbSystem, §F.7.

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsMeleeComponents.h"
#include "FlecsWeaponComponents.h"   // FEquippedBy
#include "FlecsResourceTypes.h"

void UFlecsArtillerySubsystem::SetupBlockAbsorbSystem()
{
	flecs::world& World = *FlecsWorld;

	// Pre-cached query: find a character's equipped melee weapon for guard-break.
	flecs::query<FMeleeWeaponInstance, const FEquippedBy>
		MeleeWeaponQuery = World.query_builder<FMeleeWeaponInstance, const FEquippedBy>()
			.with<FTagMeleeWeapon>()
			.build();

	World.system<FPendingBlockAbsorb, FResourcePools>("BlockAbsorbSystem")
		.without<FTagDead>()
		.each([MeleeWeaponQuery](flecs::entity Entity,
			FPendingBlockAbsorb& Pending,
			FResourcePools& Pools)
		{
			const uint64 CharId = Entity.id();

			// ── Step 1: consume stamina (clamp to 0 — no negative pools). ──
			const int32 StaminaIdx = Pools.FindPool(EResourceTypeId::Stamina);
			if (StaminaIdx != INDEX_NONE)
			{
				FResourcePool& StamPool = Pools.Pools[StaminaIdx];
				StamPool.CurrentValue = FMath::Max(0.f, StamPool.CurrentValue - Pending.TotalStaminaCost);
				// Reset regen delay — blocking consumes stamina, same as Consume().
				StamPool.RegenDelayTimer = StamPool.RegenDelay;

				// ── Step 2: guard break on stamina depletion. ──
				if (StamPool.CurrentValue <= 0.f)
				{
					// Remove blocking tag from character.
					if (Entity.has<FTagMeleeBlocking>())
					{
						Entity.remove<FTagMeleeBlocking>();
					}

					// Clear bIsBlocking on the character's melee weapon.
					MeleeWeaponQuery.each(
						[CharId](flecs::entity, FMeleeWeaponInstance& WeapInst,
							const FEquippedBy& Eq)
						{
							if (static_cast<uint64>(Eq.CharacterEntityId) == CharId
								&& WeapInst.bIsBlocking)
							{
								WeapInst.bIsBlocking = false;
							}
						});
				}
			}
			else
			{
				// No stamina pool on this entity — cannot sustain block. Break guard.
				if (Entity.has<FTagMeleeBlocking>())
				{
					Entity.remove<FTagMeleeBlocking>();
				}

				MeleeWeaponQuery.each(
					[CharId](flecs::entity, FMeleeWeaponInstance& WeapInst,
						const FEquippedBy& Eq)
					{
						if (static_cast<uint64>(Eq.CharacterEntityId) == CharId
							&& WeapInst.bIsBlocking)
						{
							WeapInst.bIsBlocking = false;
						}
					});
			}

			// ── Step 3: remove the pending component — consumed this tick. ──
			Entity.remove<FPendingBlockAbsorb>();
		});
}
