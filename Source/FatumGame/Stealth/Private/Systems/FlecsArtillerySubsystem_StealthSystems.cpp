// FlecsArtillerySubsystem - Stealth Systems
// StealthUpdateSystem: computes LightLevel, NoiseLevel, Detectability per character each sim tick.

#include "FlecsArtillerySubsystem.h"
#include "FlecsStealthComponents.h"

// Debug log throttle (log every N ticks, not every frame)
namespace { uint32 StealthDebugTickCounter = 0; constexpr uint32 StealthDebugLogInterval = 120; }
#include "FlecsMovementComponents.h"
#include "FlecsMovementStatic.h"
#include "FlecsBarrageComponents.h"
#include "FlecsGameTags.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "EPhysicsLayer.h"
#include "PhysicsFilters/FastObjectLayerFilters.h"
#include "PhysicsFilters/FastBroadphaseLayerFilter.h"

// ═══════════════════════════════════════════════════════════════
// PUBLIC API (game thread → sim thread via EnqueueCommand)
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::SetAmbientLightLevel(float Level)
{
	float Clamped = FMath::Clamp(Level, 0.f, 1.f);
	EnqueueCommand([this, Clamped]() { AmbientLightLevel = Clamped; });
}

void UFlecsArtillerySubsystem::QueueNoiseEvent(FSkeletonKey CharacterKey, const FNoiseEvent& Event)
{
	FNoiseEvent Copy = Event;
	EnqueueCommand([this, CharacterKey, Copy]()
	{
		flecs::entity Entity = GetEntityForBarrageKey(CharacterKey);
		if (!Entity.is_valid()) return;

		FStealthInstance* Stealth = Entity.try_get_mut<FStealthInstance>();
		if (!Stealth) return;

		Stealth->PendingNoise.Add(Copy);
	});
}

// ═══════════════════════════════════════════════════════════════
// STEALTH UPDATE SYSTEM
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::SetupStealthSystems()
{
	flecs::world& World = *FlecsWorld;

	// Pre-build queries for lights and noise zones (captured by value into system lambda)
	auto LightQuery = World.query_builder<const FStealthLightStatic, const FWorldPosition>()
		.with<FTagStealthLight>()
		.without<FTagDead>()
		.build();

	auto NoiseZoneQuery = World.query_builder<const FNoiseZoneStatic, const FWorldPosition>()
		.with<FTagNoiseZone>()
		.build();

	World.system<FStealthInstance, const FMovementState, const FBarrageBody>("StealthUpdateSystem")
		.with<FTagCharacter>()
		.without<FTagDead>()
		.each([this, LightQuery, NoiseZoneQuery](flecs::entity Entity, FStealthInstance& Stealth,
			const FMovementState& MoveState, const FBarrageBody& Body)
	{
		EnsureBarrageAccess();

		if (!CachedBarrageDispatch) return;

		const float DT = Entity.world().delta_time();
		if (DT <= 0.f) return;

		// Get player physics primitive
		FBLet Prim = CachedBarrageDispatch->GetShapeRef(Body.BarrageKey);
		if (!FBarragePrimitive::IsNotNull(Prim)) return;

		const FVector PlayerPos = FVector(FBarragePrimitive::GetPosition(Prim));

		// 3 sample points for shadow raycasts (feet, torso, head)
		const FVector Samples[3] = {
			PlayerPos + FVector(0, 0, StealthConstants::SampleFeetZ),
			PlayerPos + FVector(0, 0, StealthConstants::SampleTorsoZ),
			PlayerPos + FVector(0, 0, StealthConstants::SampleHeadZ)
		};

		// Pre-build raycast filters (reused for all lights):
		// Broad phase: only NON_MOVING broadphase bucket (walls, floors)
		auto ShadowBPFilter = CachedBarrageDispatch->GetDefaultBroadPhaseLayerFilter(Layers::CAST_QUERY_LEVEL_GEOMETRY_ONLY);
		// Object layer: only NON_MOVING objects
		auto ShadowObjFilter = UBarrageDispatch::GetFilterForSpecificObjectLayerOnly(Layers::NON_MOVING);
		// Body filter: ignore the character's own body
		auto ShadowBodyFilter = CachedBarrageDispatch->GetFilterToIgnoreSingleBody(Prim->KeyIntoBarrage);

		// ═══════════════════════════════════════════════════════════════
		// LIGHT LEVEL
		// ═══════════════════════════════════════════════════════════════
		float RawLight = 0.f;

		LightQuery.each([&](flecs::entity LightEntity,
			const FStealthLightStatic& Light, const FWorldPosition& LightPos)
		{
			const FVector LPos = LightPos.Position;
			const FVector ToPlayer = PlayerPos - LPos;
			const float DistSq = ToPlayer.SizeSquared();

			// Distance cull
			if (DistSq > Light.Radius * Light.Radius) return;

			// Distance attenuation: saturate(1 - (d/R)^2)^2
			const float NormDistSq = DistSq / (Light.Radius * Light.Radius);
			const float DistAtt = FMath::Square(FMath::Max(0.f, 1.f - NormDistSq));

			// Cone attenuation (Spot only)
			float ConeAtt = 1.f;
			if (Light.Type == EStealthLightType::Spot)
			{
				const float Dist = FMath::Sqrt(DistSq);
				if (Dist > KINDA_SMALL_NUMBER)
				{
					const FVector DirToPlayer = ToPlayer / Dist;
					const float CosAngle = FVector::DotProduct(Light.Direction, DirToPlayer);

					if (Light.InnerConeAngleCos > Light.OuterConeAngleCos + KINDA_SMALL_NUMBER)
					{
						const float T = FMath::Clamp(
							(CosAngle - Light.OuterConeAngleCos) /
							(Light.InnerConeAngleCos - Light.OuterConeAngleCos),
							0.f, 1.f);
						ConeAtt = T * T * (3.f - 2.f * T); // smoothstep
					}
					else
					{
						ConeAtt = CosAngle >= Light.OuterConeAngleCos ? 1.f : 0.f;
					}
				}
			}

			// Shadow occlusion via Jolt raycasts (3 sample points)
			float Occlusion = 0.f;
			if (DistSq > StealthConstants::ShadowOcclusionMinDistSq)
			{
				int32 OccludedCount = 0;
				for (int32 i = 0; i < 3; ++i)
				{
					// CastRay direction param = direction * distance (NOT unit vector)
					const FVector RayDir = LPos - Samples[i];
					const float RayLength = RayDir.Size();
					if (RayLength < KINDA_SMALL_NUMBER) continue;

					TSharedPtr<FHitResult> Hit = MakeShared<FHitResult>();
					CachedBarrageDispatch->CastRay(
						Samples[i], RayDir,
						ShadowBPFilter, ShadowObjFilter, ShadowBodyFilter,
						Hit);

					// If hit something before reaching the light, it's occluded
					if (Hit->bBlockingHit && Hit->Distance < RayLength - 1.f)
					{
						OccludedCount++;
					}
				}
				Occlusion = static_cast<float>(OccludedCount) / 3.f;
			}

			const float Contribution = Light.Intensity * DistAtt * ConeAtt * (1.f - Occlusion);
			RawLight += Contribution;
		});

		// Add ambient and clamp
		RawLight = FMath::Clamp(RawLight + AmbientLightLevel, 0.f, 1.f);
		Stealth.RawLightLevel = RawLight;

		// Asymmetric smoothing (fast enter light, faster enter shadow)
		const float Tau = (RawLight > Stealth.LightLevel)
			? StealthConstants::TauLightEnter
			: StealthConstants::TauShadowEnter;
		const float SmoothAlpha = 1.f - FMath::Exp(-DT / Tau);
		Stealth.LightLevel += (RawLight - Stealth.LightLevel) * SmoothAlpha;

		// ═══════════════════════════════════════════════════════════════
		// POSTURE & MOVEMENT MODIFIERS
		// ═══════════════════════════════════════════════════════════════
		const float PostureMod = GetPostureStealthModifier(MoveState.Posture);

		const FMovementStatic* MS = Entity.try_get<FMovementStatic>();
		const float MaxSpeed = MS ? MS->SprintSpeed : 600.f;
		const float NormSpeed = FMath::Clamp(MoveState.Speed / FMath::Max(MaxSpeed, 1.f), 0.f, 1.f);
		const float MoveMod = FMath::Max(StealthConstants::MovementModStationary,
			FMath::Pow(NormSpeed, StealthConstants::MovementSpeedExponent));

		// ═══════════════════════════════════════════════════════════════
		// NOISE
		// ═══════════════════════════════════════════════════════════════
		for (const FNoiseEvent& NE : Stealth.PendingNoise)
		{
			// Find surface zone (last zone that contains the player wins)
			ESurfaceNoise SurfaceNoise = ESurfaceNoise::Normal;
			NoiseZoneQuery.each([&](flecs::entity ZoneE, const FNoiseZoneStatic& Zone, const FWorldPosition& ZonePos)
			{
				if (Zone.ContainsPoint(PlayerPos, ZonePos.Position))
				{
					SurfaceNoise = Zone.SurfaceType;
				}
			});

			const float SurfaceMod = GetSurfaceNoiseModifier(SurfaceNoise);
			const float PostureNoiseMod = GetPostureStealthModifier(MoveState.Posture);
			const float NoiseContrib = NE.BaseLoudness * SurfaceMod * PostureNoiseMod;
			Stealth.NoiseLevel = FMath::Clamp(Stealth.NoiseLevel + NoiseContrib, 0.f, 1.f);
		}
		Stealth.PendingNoise.Reset(); // clear processed events, keep allocation

		// Noise decay
		Stealth.NoiseLevel = FMath::Max(0.f, Stealth.NoiseLevel - StealthConstants::NoiseDecayRate * DT);

		// ═══════════════════════════════════════════════════════════════
		// DETECTABILITY
		// ═══════════════════════════════════════════════════════════════
		const float EquipMod = StealthConstants::EquipmentModDefault;
		Stealth.Detectability = Stealth.LightLevel * PostureMod * MoveMod * EquipMod;

		// Throttled debug log (every 2s)
		if (++StealthDebugTickCounter >= StealthDebugLogInterval)
		{
			StealthDebugTickCounter = 0;
			UE_LOG(LogStealth, Log, TEXT("Stealth [%llu]: Light=%.2f (Raw=%.2f) Noise=%.2f Detect=%.2f | Posture=%d Speed=%.0f Ambient=%.2f"),
				Entity.id(), Stealth.LightLevel, Stealth.RawLightLevel,
				Stealth.NoiseLevel, Stealth.Detectability,
				MoveState.Posture, MoveState.Speed, AmbientLightLevel);
		}
	});
}
