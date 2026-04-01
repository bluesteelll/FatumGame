// FlecsArtillerySubsystem - Penetration System
// Processes FTagCollisionPenetration pairs BEFORE DamageCollisionSystem.
// If penetration succeeds: teleport projectile to exit, reduce damage/velocity, apply partial damage.
// If penetration fails: leave pair for DamageCollisionSystem to handle normally.

#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FlecsBarrageComponents.h"
#include "FlecsPenetrationComponents.h"
#include "FlecsProjectileComponents.h"
#include "FlecsHealthComponents.h"
#include "FlecsDestructibleComponents.h"
#include "FlecsExplosionComponents.h"
#include "FlecsGameTags.h"
#include "EPhysicsLayer.h"
#include "PhysicsFilters/FastObjectLayerFilters.h"

void UFlecsArtillerySubsystem::SetupPenetrationSystem()
{
	flecs::world& World = *FlecsWorld;

	// ─────────────────────────────────────────────────────────
	// PENETRATION SYSTEM
	// Runs BEFORE DamageCollisionSystem. Decides whether a
	// penetrating projectile passes through or stops.
	// ─────────────────────────────────────────────────────────
	World.system<const FCollisionPair>("PenetrationSystem")
		.with<FTagCollisionPenetration>()
		.without<FTagCollisionProcessed>()
		.each([this, &World](flecs::entity PairEntity, const FCollisionPair& Pair)
		{
			EnsureBarrageAccess();

			// ── Identify projectile and target ──
			const uint64 ProjectileId = Pair.GetProjectileEntityId();
			const uint64 TargetId = Pair.GetTargetEntityId();
			if (ProjectileId == 0) return;

			flecs::entity ProjEntity = World.entity(ProjectileId);
			if (!ProjEntity.is_valid() || !ProjEntity.is_alive() || ProjEntity.has<FTagDead>()) return;

			// Explosives NEVER penetrate — they detonate
			if (ProjEntity.try_get<FExplosionStatic>()) return;

			const FPenetrationStatic* PenStatic = ProjEntity.try_get<FPenetrationStatic>();
			if (!PenStatic) return;

			FPenetrationInstance* PenInstance = ProjEntity.try_get_mut<FPenetrationInstance>();
			if (!PenInstance) return;

			// Max penetrations reached
			if (PenStatic->MaxPenetrations >= 0 && PenInstance->PenetrationCount >= PenStatic->MaxPenetrations)
				return;

			// Budget exhausted
			if (PenInstance->RemainingBudget <= 0.01f) return;

			// ── Target validation ──
			if (TargetId == 0) return;  // No entity — impenetrable (BounceSystem will kill)

			flecs::entity TargetEntity = World.entity(TargetId);
			if (!TargetEntity.is_valid() || !TargetEntity.is_alive()) return;

			const FPenetrationMaterial* PenMaterial = TargetEntity.try_get<FPenetrationMaterial>();
			if (!PenMaterial) return;  // No material = impenetrable

			// Owner check — don't penetrate self
			const FProjectileInstance* ProjInst = ProjEntity.try_get<FProjectileInstance>();
			if (ProjInst && ProjInst->IsOwnedBy(TargetId))
			{
				PairEntity.add<FTagCollisionProcessed>();
				return;
			}

			// ── Get projectile physics body ──
			const FBarrageBody* ProjBody = ProjEntity.try_get<FBarrageBody>();
			if (!ProjBody || !ProjBody->IsValid() || !CachedBarrageDispatch) return;

			FBLet ProjPrim = CachedBarrageDispatch->GetShapeRef(ProjBody->BarrageKey);
			if (!FBarragePrimitive::IsNotNull(ProjPrim)) return;

			// Use pre-collision velocity captured in contact listener
			const FVector IncomingVelocity = Pair.IncomingVelocity;
			const float IncomingSpeed = IncomingVelocity.Size();
			if (IncomingSpeed < 1.f) return;  // Stopped

			const FVector IncomingDir = IncomingVelocity / IncomingSpeed;

			// ── Incidence angle ──
			FVector SurfaceNormal = Pair.ContactNormal;
			// Ensure normal points against the projectile direction
			if (FVector::DotProduct(IncomingDir, SurfaceNormal) > 0.f)
				SurfaceNormal = -SurfaceNormal;

			const float CosAngle = FMath::Abs(FVector::DotProduct(IncomingDir, SurfaceNormal));

			// Ricochet check: use more restrictive threshold
			const float RicochetThreshold = FMath::Max(
				PenStatic->RicochetCosAngleThreshold,
				PenMaterial->RicochetCosAngleThreshold);
			if (CosAngle < RicochetThreshold) return;  // Too oblique — let bounce/damage handle

			// ── Reverse raycast for thickness measurement ──
			const FVector EntryPoint = Pair.ContactPoint;

			// MaxProbe: limited by remaining budget and material resistance
			const float MaxProbeDistance = FMath::Min(
				PenInstance->RemainingBudget * 2.f / FMath::Max(PenMaterial->MaterialResistance, 0.01f),
				500.f);  // Cap at 5m

			const FVector FarPoint = EntryPoint + IncomingDir * (MaxProbeDistance + 10.f);

			// Cast backwards from far side — filter only static and moving geometry
			FastIncludeObjectLayerFilter ThicknessObjFilter({
				EPhysicsLayer::NON_MOVING,
				EPhysicsLayer::MOVING
			});
			auto ThicknessBPFilter = CachedBarrageDispatch->GetDefaultBroadPhaseLayerFilter(Layers::CAST_QUERY);
			JPH::BodyFilter ThicknessBodyFilter;

			TSharedPtr<FHitResult> ReverseHit = MakeShared<FHitResult>();
			const FVector ReverseRayDir = -IncomingDir * (MaxProbeDistance + 10.f);
			CachedBarrageDispatch->CastRay(
				FarPoint, ReverseRayDir,
				ThicknessBPFilter, ThicknessObjFilter, ThicknessBodyFilter,
				ReverseHit);

			float PhysicalThickness;
			FVector ExitPoint;
			if (ReverseHit->bBlockingHit)
			{
				ExitPoint = ReverseHit->ImpactPoint;
				PhysicalThickness = FVector::Dist(EntryPoint, ExitPoint);
			}
			else
			{
				// Reverse ray missed — assume thin surface (e.g. single-sided mesh)
				PhysicalThickness = 5.f;
				ExitPoint = EntryPoint + IncomingDir * PhysicalThickness;
			}

			// Clamp minimum thickness
			PhysicalThickness = FMath::Max(PhysicalThickness, 0.1f);

			// ── Effective thickness (material + angle) ──
			const float EffectiveThickness = PhysicalThickness * PenMaterial->MaterialResistance
				/ FMath::Max(CosAngle, 0.1f);

			if (EffectiveThickness >= PenInstance->RemainingBudget)
				return;  // Cannot penetrate — DamageCollisionSystem will handle

			// ════════════════════════════════════════════════════════
			// PENETRATE
			// ════════════════════════════════════════════════════════

			const float BudgetFraction = EffectiveThickness / PenStatic->PenetrationBudget;
			const float DamageMultiplier = FMath::Max(0.f,
				1.f - BudgetFraction * PenStatic->DamageFalloffFactor);
			const float VelocityMultiplier = FMath::Max(0.1f,
				1.f - BudgetFraction * PenStatic->VelocityFalloffFactor);

			// ── Apply reduced damage to target ──
			const FDamageStatic* DmgStatic = ProjEntity.try_get<FDamageStatic>();
			if (DmgStatic && TargetEntity.has<FHealthInstance>() && !TargetEntity.has<FTagDead>())
			{
				const float ReducedDamage = DmgStatic->Damage
					* PenInstance->CurrentDamageMultiplier * DamageMultiplier;
				if (ReducedDamage > 0.f)
				{
					FPendingDamage& Pending = TargetEntity.obtain<FPendingDamage>();
					Pending.AddHit(ReducedDamage, ProjectileId, DmgStatic->DamageType,
						Pair.ContactPoint, false, false);
					TargetEntity.modified<FPendingDamage>();
				}
			}

			// ── Trigger fragmentation on destructibles ──
			const FDestructibleStatic* DestrStatic = TargetEntity.try_get<FDestructibleStatic>();
			if (DestrStatic && DestrStatic->IsValid() && !TargetEntity.has<FPendingFragmentation>())
			{
				FPendingFragmentation Frag;
				Frag.ImpactPoint = Pair.ContactPoint;
				Frag.ImpactDirection = IncomingDir;
				Frag.ImpactImpulse = IncomingSpeed * PenStatic->ImpulseTransferFactor;
				TargetEntity.set<FPendingFragmentation>(Frag);
				UE_LOG(LogTemp, Log, TEXT("PENETRATION_FRAG: Target %llu ImpactImpulse=%.1f (Speed=%.1f × Transfer=%.2f) Dir=(%.2f,%.2f,%.2f)"),
					TargetId, Frag.ImpactImpulse, IncomingSpeed, PenStatic->ImpulseTransferFactor,
					IncomingDir.X, IncomingDir.Y, IncomingDir.Z);
			}
			else if (DestrStatic)
			{
				UE_LOG(LogTemp, Warning, TEXT("PENETRATION_FRAG: Target %llu skipped — Valid=%d HasPending=%d"),
					TargetId, DestrStatic->IsValid(), TargetEntity.has<FPendingFragmentation>());
			}

			// ── Teleport projectile to exit point ──
			const FVector ExitLocation = ExitPoint + IncomingDir * 5.f;  // 5cm past exit to clear surface
			const FBarrageKey ProjBarrageKey = ProjPrim->KeyIntoBarrage;
			CachedBarrageDispatch->SetBodyPositionDirect(ProjBarrageKey, ExitLocation);

			// Set reduced velocity (immediate, not queued)
			const FVector NewVelocity = IncomingDir * IncomingSpeed * VelocityMultiplier;
			CachedBarrageDispatch->SetBodyLinearVelocityDirect(ProjBarrageKey, NewVelocity);

			// ── Update penetration instance ──
			PenInstance->RemainingBudget -= EffectiveThickness;
			PenInstance->PenetrationCount++;
			PenInstance->CurrentDamageMultiplier *= DamageMultiplier;
			PenInstance->LastPenetratedTargetId = TargetId;  // Suppress re-contact kill with same target

			// Mark pair as processed — downstream systems will skip it
			PairEntity.add<FTagCollisionProcessed>();

			UE_LOG(LogTemp, Log,
				TEXT("PENETRATION: Projectile %llu through target %llu "
				     "Thick=%.1f EffThick=%.1f Budget=%.1f->%.1f DmgMult=%.2f VelMult=%.2f Count=%d"),
				ProjectileId, TargetId,
				PhysicalThickness, EffectiveThickness,
				PenInstance->RemainingBudget + EffectiveThickness, PenInstance->RemainingBudget,
				DamageMultiplier, VelocityMultiplier, PenInstance->PenetrationCount);
		});
}
