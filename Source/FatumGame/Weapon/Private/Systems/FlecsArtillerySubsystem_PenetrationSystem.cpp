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
#include "IsolatedJoltIncludes.h"
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

			// ── Determine material (SubShapeID for compound, fallback to FPenetrationMaterial) ──
			const FPenetrationMaterial* PenMaterial = TargetEntity.try_get<FPenetrationMaterial>();
			float MaterialResistanceFromSubShape = -1.f;
			EPenetrationMaterialCategory SubShapeMaterialCat = EPenetrationMaterialCategory::Impenetrable;

			if (Pair.SubShapeID2 != 0 && CachedBarrageDispatch)
			{
				const FBarrageBody* TargetBody = TargetEntity.try_get<FBarrageBody>();
				if (TargetBody && TargetBody->IsValid())
				{
					FBLet TargetPrimSS = CachedBarrageDispatch->GetShapeRef(TargetBody->BarrageKey);
					if (FBarragePrimitive::IsNotNull(TargetPrimSS))
					{
						uint32 SubShapeData = CachedBarrageDispatch->GetSubShapeUserData(
							TargetPrimSS->KeyIntoBarrage, Pair.SubShapeID2);
						// SubShapeData uses +1 offset: 0 = no data (non-compound), 1 = Impenetrable, 2 = Flesh, etc.
						if (SubShapeData > 0)
						{
							SubShapeMaterialCat = static_cast<EPenetrationMaterialCategory>((SubShapeData - 1) & 0xFF);
							MaterialResistanceFromSubShape = GetResistanceForCategory(SubShapeMaterialCat);
							UE_LOG(LogTemp, Log, TEXT("PENETRATION: SubShape material=%d resistance=%.1f (SubShapeID=%u)"),
								static_cast<uint8>(SubShapeMaterialCat), MaterialResistanceFromSubShape, Pair.SubShapeID2);
						}
					}
				}
			}

			// Need at least one material source
			if (!PenMaterial && MaterialResistanceFromSubShape < 0.f) return;

			// SubShape says Impenetrable → this part of compound cannot be penetrated
			if (MaterialResistanceFromSubShape >= 900.f) return;

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
			const float TargetRicochetThreshold = PenMaterial ? PenMaterial->RicochetCosAngleThreshold : 0.26f;
			const float RicochetThreshold = FMath::Max(
				PenStatic->RicochetCosAngleThreshold,
				TargetRicochetThreshold);
			if (CosAngle < RicochetThreshold) return;  // Too oblique — let bounce/damage handle

			// ── Reverse raycast for thickness measurement ──
			const FVector EntryPoint = Pair.ContactPoint;

			// Material resistance: prefer SubShape (compound), fallback to FPenetrationMaterial
			const float MaterialResistance = (MaterialResistanceFromSubShape >= 0.f)
				? MaterialResistanceFromSubShape
				: (PenMaterial ? PenMaterial->GetResistance() : 999.f);
			const float MaxProbeDistance = FMath::Min(
				PenInstance->RemainingBudget * 2.f / FMath::Max(MaterialResistance, 0.01f),
				500.f);

			// ── Analytical thickness: ray-AABB slab intersection ──
			// ~10 ns, zero allocations, no physics queries.
			// Computes exact distance the ray travels through the body's AABB.
			float PhysicalThickness = 5.f;  // fallback
			FVector ExitPoint = EntryPoint + IncomingDir * PhysicalThickness;

			{
				const FBarrageBody* TargetBody = TargetEntity.try_get<FBarrageBody>();
				if (TargetBody && TargetBody->IsValid())
				{
					FBLet TargetPrimThick = CachedBarrageDispatch->GetShapeRef(TargetBody->BarrageKey);
					JPH::Vec3 JoltMin, JoltMax;
					if (FBarragePrimitive::IsNotNull(TargetPrimThick)
						&& CachedBarrageDispatch->GetBodyWorldBoundsJolt(TargetPrimThick->KeyIntoBarrage, JoltMin, JoltMax))
					{
						// Jolt AABB → UE world coords
						const FVector AABBMin(JoltMin.GetX() * 100.f, JoltMin.GetZ() * 100.f, JoltMin.GetY() * 100.f);
						const FVector AABBMax(JoltMax.GetX() * 100.f, JoltMax.GetZ() * 100.f, JoltMax.GetY() * 100.f);

						// Slab method: ray vs AABB → entry/exit parametric distances
						float tMin = -1e10f, tMax = 1e10f;
						for (int32 Axis = 0; Axis < 3; ++Axis)
						{
							const float Dir = IncomingDir[Axis];
							const float Origin = EntryPoint[Axis];
							if (FMath::Abs(Dir) > 1e-6f)
							{
								const float InvDir = 1.f / Dir;
								float t0 = (AABBMin[Axis] - Origin) * InvDir;
								float t1 = (AABBMax[Axis] - Origin) * InvDir;
								if (t0 > t1) Swap(t0, t1);
								tMin = FMath::Max(tMin, t0);
								tMax = FMath::Min(tMax, t1);
							}
						}

						if (tMax > tMin && tMax > 0.f)
						{
							// tMin = entry distance (may be negative if inside), tMax = exit distance
							const float Entry = FMath::Max(tMin, 0.f);
							PhysicalThickness = tMax - Entry;
							ExitPoint = EntryPoint + IncomingDir * tMax;
						}
					}
				}
				PhysicalThickness = FMath::Max(PhysicalThickness, 0.1f);
			}

			// ── Effective thickness (material + angle) ──
			// Surface integrity grid: degrade effective resistance at this cell
			float EffectiveResistance = MaterialResistance;
			FSurfaceIntegrity* Grid = TargetEntity.try_get_mut<FSurfaceIntegrity>();
			int32 GridCellIdx = -1;
			FVector BodyPos = FVector::ZeroVector;
			FQuat BodyRot = FQuat::Identity;

			{
				const FBarrageBody* TargetBody = TargetEntity.try_get<FBarrageBody>();
				if (TargetBody && TargetBody->IsValid())
				{
					FBLet TargetPrim = CachedBarrageDispatch->GetShapeRef(TargetBody->BarrageKey);
					if (FBarragePrimitive::IsNotNull(TargetPrim))
					{
						BodyPos = FVector(FBarragePrimitive::GetPosition(TargetPrim));
						BodyRot = FQuat(FBarragePrimitive::OptimisticGetAbsoluteRotation(TargetPrim));

						if (Grid)
						{
							GridCellIdx = Grid->WorldToCell(Pair.ContactPoint, BodyPos, BodyRot);
							const float LocalIntegrity = Grid->GetIntegrity(GridCellIdx);
							EffectiveResistance = MaterialResistance * IntegrityToResistance(LocalIntegrity);
						}
					}
				}
			}

			const float EffectiveThickness = PhysicalThickness * EffectiveResistance
				/ FMath::Max(CosAngle, 0.1f);

			UE_LOG(LogTemp, Log, TEXT("PENETRATION_CALC: Thick=%.1f EffRes=%.2f CosAngle=%.3f EffThick=%.1f Budget=%.1f %s"),
				PhysicalThickness, EffectiveResistance, CosAngle, EffectiveThickness, PenInstance->RemainingBudget,
				EffectiveThickness < PenInstance->RemainingBudget ? TEXT("WILL_PENETRATE") : TEXT("BLOCKED"));

			// ── Surface degradation (runs on EVERY hit, penetrate or not) ──
			const FDamageStatic* DmgStatic = ProjEntity.try_get<FDamageStatic>();
			const float BulletDamage = DmgStatic ? DmgStatic->Damage : 25.f;
			const bool bWillPenetrate = (EffectiveThickness < PenInstance->RemainingBudget);

			const bool bCanDegrade = MaterialResistance < 900.f
				&& (MaterialResistanceFromSubShape >= 0.f  // Compound sub-shape: always degradable
					|| (PenMaterial && PenMaterial->bDegradable));

			if (bCanDegrade)
			{
				if (!Grid)
				{
					// Lazy init on first hit
					const FBarrageBody* TargetBody = TargetEntity.try_get<FBarrageBody>();
					if (TargetBody && TargetBody->IsValid())
					{
						FBLet TargetPrimForAABB = CachedBarrageDispatch->GetShapeRef(TargetBody->BarrageKey);
						JPH::Vec3 JoltMin, JoltMax;
						if (FBarragePrimitive::IsNotNull(TargetPrimForAABB)
							&& CachedBarrageDispatch->GetBodyWorldBoundsJolt(TargetPrimForAABB->KeyIntoBarrage, JoltMin, JoltMax))
						{
							// Jolt AABB (meters, Y-up) → UE world (cm, Z-up): (X*100, Z*100, Y*100)
							FVector AABBMinUE(JoltMin.GetX() * 100.f, JoltMin.GetZ() * 100.f, JoltMin.GetY() * 100.f);
							FVector AABBMaxUE(JoltMax.GetX() * 100.f, JoltMax.GetZ() * 100.f, JoltMax.GetY() * 100.f);
							FVector LocalMin = BodyRot.UnrotateVector(AABBMinUE - BodyPos);
							FVector LocalMax = BodyRot.UnrotateVector(AABBMaxUE - BodyPos);
							FVector TrueMin(FMath::Min(LocalMin.X, LocalMax.X), FMath::Min(LocalMin.Y, LocalMax.Y), FMath::Min(LocalMin.Z, LocalMax.Z));
							FVector TrueMax(FMath::Max(LocalMin.X, LocalMax.X), FMath::Max(LocalMin.Y, LocalMax.Y), FMath::Max(LocalMin.Z, LocalMax.Z));

							FSurfaceIntegrity NewGrid;
							const uint8 GCols = PenMaterial ? PenMaterial->GridCols : 0;
							const uint8 GRows = PenMaterial ? PenMaterial->GridRows : 0;
							NewGrid.InitFromAABB(TrueMin, TrueMax, GCols, GRows);
							TargetEntity.set<FSurfaceIntegrity>(NewGrid);
							Grid = TargetEntity.try_get_mut<FSurfaceIntegrity>();
							if (Grid) GridCellIdx = Grid->WorldToCell(Pair.ContactPoint, BodyPos, BodyRot);
						}
					}
				}

				if (Grid && GridCellIdx >= 0)
				{
					// Degrade rate: prefer per-material (from cached sub-shape category or profile)
					float DegradeRate;
					if (MaterialResistanceFromSubShape >= 0.f)
					{
						DegradeRate = GetDegradeRateForCategory(SubShapeMaterialCat);
					}
					else if (PenMaterial)
					{
						// Profile override or default from material category
						DegradeRate = (PenMaterial->BaseDegradeRate != 0.08f)
							? PenMaterial->BaseDegradeRate  // Designer overrode in profile
							: GetDegradeRateForCategory(PenMaterial->MaterialCategory);
					}
					else
					{
						DegradeRate = 0.08f;
					}

					const float SpreadFactor = PenMaterial ? PenMaterial->DegradeSpreadFactor : 0.3f;
					float NormDegrade = DegradeRate * (BulletDamage / 25.f);
					if (bWillPenetrate) NormDegrade *= 0.5f;
					Grid->DegradeWithSpread(GridCellIdx, NormDegrade, SpreadFactor);

					// Fragmentation trigger: cell below 5% on destructible
					if (Grid->GetIntegrity(GridCellIdx) < 0.05f)
					{
						const FDestructibleStatic* DestrCheck = TargetEntity.try_get<FDestructibleStatic>();
						if (DestrCheck && DestrCheck->IsValid() && !TargetEntity.has<FPendingFragmentation>())
						{
							FPendingFragmentation Frag;
							Frag.ImpactPoint = Pair.ContactPoint;
							Frag.ImpactDirection = IncomingDir;
							Frag.ImpactImpulse = IncomingSpeed * 0.3f;
							TargetEntity.set<FPendingFragmentation>(Frag);
						}
					}
				}
			}

			if (!bWillPenetrate)
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
