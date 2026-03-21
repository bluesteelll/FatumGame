// FlecsArtillerySubsystem - Vitals Systems
// EquipmentModifierSystem: scans inventory for passive vitals bonuses.
// VitalDrainSystem: drains hunger/thirst, lerps warmth toward environment temperature.
// VitalModifierRecalcSystem: recomputes stat modifiers from threshold severity.
// VitalHPDrainSystem: applies HP drain from vitals penalties.

#include "FlecsArtillerySubsystem.h"
#include "FlecsVitalsComponents.h"
#include "FlecsStealthComponents.h"   // FWorldPosition
#include "FlecsBarrageComponents.h"   // FBarrageBody
#include "FBarragePrimitive.h"
#include "BarrageDispatch.h"
#include "FlecsGameTags.h"
#include "FlecsItemComponents.h"      // FContainedIn
#include "FlecsHealthComponents.h"    // FHealthInstance

// ═══════════════════════════════════════════════════════════════
// PUBLIC API (game thread -> sim thread via EnqueueCommand)
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::SetDefaultAmbientTemperature(float Temperature)
{
	float Clamped = FMath::Clamp(Temperature, 0.f, 1.f);
	EnqueueCommand([this, Clamped]() { DefaultAmbientTemperature = Clamped; });
}

// ═══════════════════════════════════════════════════════════════
// VITALS SYSTEMS
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::SetupVitalsSystems()
{
	flecs::world& World = *FlecsWorld;
	auto* Subsystem = this;

	// ─────────────────────────────────────────────────────────
	// EQUIPMENT MODIFIER SYSTEM
	// Scans inventory items for passive vitals bonuses when dirty.
	// ─────────────────────────────────────────────────────────

	// Pre-build query for items with vitals data that are in a container
	auto ItemVitalsQuery = World.query_builder<const FContainedIn, const FVitalsItemStatic>()
		.build();

	World.system<FVitalsInstance, FEquipmentVitalsCache, const FCharacterInventoryRef>("EquipmentModifierSystem")
		.with<FTagCharacter>()
		.without<FTagDead>()
		.each([ItemVitalsQuery](flecs::entity E, FVitalsInstance& VI, FEquipmentVitalsCache& Cache,
			const FCharacterInventoryRef& InvRef)
		{
			if (!VI.bEquipmentDirty) return;
			VI.bEquipmentDirty = false;

			// Reset cache to defaults
			Cache.WarmthBonus = 0.f;
			Cache.HungerDrainMult = 1.f;
			Cache.ThirstDrainMult = 1.f;

			if (InvRef.InventoryEntityId == 0) return;

			const int64 TargetContainerId = InvRef.InventoryEntityId;

			ItemVitalsQuery.each([&](flecs::entity ItemE, const FContainedIn& CI, const FVitalsItemStatic& VIS)
			{
				if (CI.ContainerEntityId != TargetContainerId) return;
				if (!VIS.HasPassiveEffect()) return;

				Cache.WarmthBonus += VIS.PassiveWarmthBonus;
				Cache.HungerDrainMult *= VIS.PassiveHungerDrainMult;
				Cache.ThirstDrainMult *= VIS.PassiveThirstDrainMult;
			});
		});

	// ─────────────────────────────────────────────────────────
	// VITAL DRAIN SYSTEM
	// Drains hunger/thirst, lerps warmth toward temperature zones.
	// Uses .run() for complex multi-query logic.
	// system<>() with no query terms — EcsQueryMatchNothing.
	// Flecs auto-fini's after run(). Do NOT call It.fini().
	// ─────────────────────────────────────────────────────────

	auto ZoneQuery = World.query_builder<const FTemperatureZoneStatic, const FWorldPosition>()
		.with<FTagTemperatureZone>()
		.build();

	auto CharVitalsQuery = World.query_builder<const FVitalsStatic, FVitalsInstance, const FEquipmentVitalsCache, const FBarrageBody>()
		.with<FTagCharacter>()
		.without<FTagDead>()
		.build();

	// Stack-allocated zone cache to avoid heap allocation per tick
	struct FCachedZone
	{
		FVector Pos;
		FVector Extent;
		float Temp;
	};

	World.system<>("VitalDrainSystem")
		.run([Subsystem, ZoneQuery, CharVitalsQuery](flecs::iter& It)
		{
			Subsystem->EnsureBarrageAccess();
			const float DT = It.delta_time();
			if (DT <= 0.f) return;

			// Collect all temperature zones into a stack-allocated array
			TArray<FCachedZone, TInlineAllocator<64>> Zones;
			ZoneQuery.each([&](const FTemperatureZoneStatic& Zone, const FWorldPosition& Pos)
			{
				FCachedZone CZ;
				CZ.Pos = Pos.Position;
				CZ.Extent = Zone.Extent;
				CZ.Temp = Zone.Temperature;
				Zones.Add(CZ);
			});

			UBarrageDispatch* Barrage = Subsystem->GetBarrageDispatch();
			if (!Barrage) return;

			CharVitalsQuery.each([&](flecs::entity E, const FVitalsStatic& VS, FVitalsInstance& VI,
				const FEquipmentVitalsCache& Cache, const FBarrageBody& Body)
			{
				// Get character position from physics body
				FBLet Prim = Barrage->GetShapeRef(Body.BarrageKey);
				if (!FBarragePrimitive::IsNotNull(Prim)) return;
				const FVector CharPos = FVector(FBarragePrimitive::GetPosition(Prim));

				// ── Temperature zone evaluation ──
				float TempSum = 0.f;
				int32 ZoneCount = 0;
				for (const auto& Z : Zones)
				{
					const FVector Delta = (CharPos - Z.Pos).GetAbs();
					if (Delta.X <= Z.Extent.X && Delta.Y <= Z.Extent.Y && Delta.Z <= Z.Extent.Z)
					{
						TempSum += Z.Temp;
						++ZoneCount;
					}
				}

				const float EnvironmentTemp = ZoneCount > 0
					? TempSum / static_cast<float>(ZoneCount)
					: Subsystem->DefaultAmbientTemperature;
				VI.TargetWarmth = FMath::Clamp(EnvironmentTemp + Cache.WarmthBonus, 0.f, 1.f);

				// ── Warmth lerp toward target ──
				const float WarmthDelta = (VI.TargetWarmth - VI.WarmthPercent) * VS.WarmthTransitionSpeed;
				VI.WarmthPercent = FMath::Clamp(VI.WarmthPercent + WarmthDelta * DT, 0.f, 1.f);

				// ── Cross-vital: cold increases hunger drain ──
				float HungerDrainScale = 1.f;
				// Find active warmth threshold for cross-vital multiplier
				for (int32 i = 3; i >= 0; --i)
				{
					if (VS.WarmthThresholds[i].Percent > 0.f && VI.WarmthPercent <= VS.WarmthThresholds[i].Percent)
					{
						HungerDrainScale = VS.WarmthThresholds[i].CrossVitalDrainMultiplier;
						break;
					}
				}

				// ── Hunger drain ──
				VI.HungerAccum += VS.HungerDrainRate * HungerDrainScale * Cache.HungerDrainMult * DT;
				if (VI.HungerAccum >= 0.001f)
				{
					VI.HungerPercent = FMath::Max(0.f, VI.HungerPercent - VI.HungerAccum);
					VI.HungerAccum = 0.f;
				}

				// ── Thirst drain ──
				VI.ThirstAccum += VS.ThirstDrainRate * Cache.ThirstDrainMult * DT;
				if (VI.ThirstAccum >= 0.001f)
				{
					VI.ThirstPercent = FMath::Max(0.f, VI.ThirstPercent - VI.ThirstAccum);
					VI.ThirstAccum = 0.f;
				}

				// ── Write to SimStateCache for UI ──
				const int64 EntityId = static_cast<int64>(E.id());
				Subsystem->GetSimStateCache().WriteVitals(EntityId, VI.HungerPercent, VI.ThirstPercent, VI.WarmthPercent);
			});
		});

	// ─────────────────────────────────────────────────────────
	// VITAL MODIFIER RECALC SYSTEM
	// Recomputes stat modifiers from current vitals threshold severity.
	// ─────────────────────────────────────────────────────────

	World.system<const FVitalsStatic, const FVitalsInstance, FStatModifiers>("VitalModifierRecalcSystem")
		.with<FTagCharacter>()
		.without<FTagDead>()
		.each([](flecs::entity E, const FVitalsStatic& VS, const FVitalsInstance& VI, FStatModifiers& Mods)
		{
			Mods.SpeedMultiplier = 1.f;
			Mods.bCanSprint = true;
			Mods.bCanJump = true;
			Mods.HPDrainPerSecond = 0.f;
			// Don't reset HPDrainAccum — it carries over between frames

			auto ApplyThresholds = [&](float Percent, const FVitalThreshold Thresholds[4])
			{
				for (int32 i = 3; i >= 0; --i)
				{
					if (Thresholds[i].Percent > 0.f && Percent <= Thresholds[i].Percent)
					{
						Mods.SpeedMultiplier *= Thresholds[i].SpeedMultiplier;
						Mods.bCanSprint = Mods.bCanSprint && Thresholds[i].bCanSprint;
						Mods.bCanJump = Mods.bCanJump && Thresholds[i].bCanJump;
						Mods.HPDrainPerSecond += Thresholds[i].HPDrainPerSecond;
						return;
					}
				}
			};

			ApplyThresholds(VI.HungerPercent, VS.HungerThresholds);
			ApplyThresholds(VI.ThirstPercent, VS.ThirstThresholds);
			ApplyThresholds(VI.WarmthPercent, VS.WarmthThresholds);
		});

	// ─────────────────────────────────────────────────────────
	// VITAL HP DRAIN SYSTEM
	// Applies HP damage from vitals penalties (starvation, dehydration, freezing).
	// ─────────────────────────────────────────────────────────

	World.system<FStatModifiers>("VitalHPDrainSystem")
		.with<FTagCharacter>()
		.with<FHealthInstance>()
		.without<FTagDead>()
		.each([Subsystem](flecs::entity E, FStatModifiers& Mods)
		{
			if (Mods.HPDrainPerSecond <= 0.f) return;

			const float DT = E.world().delta_time();
			Mods.HPDrainAccum += Mods.HPDrainPerSecond * DT;

			if (Mods.HPDrainAccum >= 1.f)
			{
				const float DrainAmount = FMath::Floor(Mods.HPDrainAccum);
				Mods.HPDrainAccum -= DrainAmount;

				// QueueDamage with bIgnoreArmor=true (environmental damage bypasses armor)
				Subsystem->QueueDamage(E, DrainAmount, 0, FGameplayTag(), FVector::ZeroVector, true);
			}
		});
}
