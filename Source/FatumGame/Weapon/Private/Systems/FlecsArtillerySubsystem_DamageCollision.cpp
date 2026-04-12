// FlecsArtillerySubsystem - Damage & Bounce Collision Systems (Weapon domain)

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsBarrageComponents.h"
#include "FlecsHealthComponents.h"
#include "FlecsProjectileComponents.h"
#include "FlecsExplosionComponents.h"
#include "FlecsPenetrationComponents.h"
#include "FlecsDestructibleComponents.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "IsolatedJoltIncludes.h"
#include "FlecsNiagaraManager.h"
#include "FlecsMessageSubsystem.h"
#include "FlecsUIMessages.h"

void UFlecsArtillerySubsystem::SetupDamageCollisionSystems()
{
	flecs::world& World = *FlecsWorld;

	// ─────────────────────────────────────────────────────────
	// DAMAGE COLLISION SYSTEM
	// ─────────────────────────────────────────────────────────
	World.system<const FCollisionPair>("DamageCollisionSystem")
		.with<FTagCollisionDamage>()
		.without<FTagCollisionProcessed>()
		.each([this, &World](flecs::entity PairEntity, const FCollisionPair& Pair)
		{
			uint64 ProjectileId = Pair.GetProjectileEntityId();
			uint64 TargetId = Pair.GetTargetEntityId();

			float Damage = 25.f;
			FGameplayTag DamageType;
			bool bAreaDamage = false;
			bool bDestroyOnHit = false;
			float CritChance = 0.f;
			float CritMultiplier = 2.f;
			int32 MaxBounces = 0;

			flecs::entity ProjectileEntity;

			if (ProjectileId != 0)
			{
				ProjectileEntity = World.entity(ProjectileId);
				if (ProjectileEntity.is_valid())
				{
					const FDamageStatic* DmgStatic = ProjectileEntity.try_get<FDamageStatic>();
					const FProjectileStatic* ProjStatic = ProjectileEntity.try_get<FProjectileStatic>();

					if (DmgStatic)
					{
						Damage = DmgStatic->Damage;
						DamageType = DmgStatic->DamageType;
						bAreaDamage = DmgStatic->bAreaDamage;
						bDestroyOnHit = DmgStatic->bDestroyOnHit;
						CritChance = DmgStatic->CritChance;
						CritMultiplier = DmgStatic->CritMultiplier;
					}

					if (ProjStatic)
					{
						MaxBounces = ProjStatic->MaxBounces;
					}
				}
			}

			if (TargetId != 0)
			{
				flecs::entity Target = World.entity(TargetId);
				if (Target.is_valid() && !Target.has<FTagDead>())
				{
					if (ProjectileEntity.is_valid())
					{
						const FProjectileInstance* ProjInst = ProjectileEntity.try_get<FProjectileInstance>();
						if (ProjInst && ProjInst->IsOwnedBy(TargetId))
						{
							PairEntity.add<FTagCollisionProcessed>();
							return;
						}
					}

					if (Target.has<FHealthInstance>())
					{
						// Use StructuralDamage vs destructible targets (if specified)
						const FDamageStatic* DmgStaticForTarget = ProjectileEntity.is_valid()
							? ProjectileEntity.try_get<FDamageStatic>() : nullptr;
						float FinalDamage = Damage;
						if (DmgStaticForTarget && DmgStaticForTarget->StructuralDamage > 0.f
							&& Target.has<FDestructibleStatic>())
						{
							FinalDamage = DmgStaticForTarget->StructuralDamage;
						}

						// Apply penetration damage reduction (bullet lost energy through obstacles)
						const FPenetrationInstance* PenInst = ProjectileEntity.is_valid()
							? ProjectileEntity.try_get<FPenetrationInstance>() : nullptr;
						if (PenInst && PenInst->CurrentDamageMultiplier < 1.f)
						{
							FinalDamage *= PenInst->CurrentDamageMultiplier;
						}

						bool bIsCritical = (CritChance > 0.f && FMath::FRand() < CritChance);
						FPendingDamage& Pending = Target.obtain<FPendingDamage>();
						Pending.AddHit(FinalDamage, ProjectileId, DamageType, Pair.ContactPoint, bIsCritical, false);
						Target.modified<FPendingDamage>();

						UE_LOG(LogTemp, Log, TEXT("COLLISION: Queued %.1f damage to Entity %llu (Crit=%d)"),
							FinalDamage, TargetId, bIsCritical);
					}

					// Surface degradation for non-penetrating bullets hitting degradable targets
					const FPenetrationMaterial* TargetPenMat = Target.try_get<FPenetrationMaterial>();
					if (TargetPenMat && TargetPenMat->bDegradable && TargetPenMat->GetResistance() < 900.f)
					{
						const float MatResistance = TargetPenMat->GetResistance();
						const float DegradeRate = GetDegradeRateForCategory(TargetPenMat->MaterialCategory);
						const float NormDegrade = DegradeRate * (Damage / 25.f);

						// Lazy init + degrade (same pattern as PenetrationSystem)
						FSurfaceIntegrity* Grid = Target.try_get_mut<FSurfaceIntegrity>();
						if (!Grid && CachedBarrageDispatch)
						{
							const FBarrageBody* TBody = Target.try_get<FBarrageBody>();
							if (TBody && TBody->IsValid())
							{
								FBLet TPrim = CachedBarrageDispatch->GetShapeRef(TBody->BarrageKey);
								JPH::Vec3 JMin, JMax;
								if (FBarragePrimitive::IsNotNull(TPrim)
									&& CachedBarrageDispatch->GetBodyWorldBoundsJolt(TPrim->KeyIntoBarrage, JMin, JMax))
								{
									FVector BPos(FBarragePrimitive::GetPosition(TPrim));
									FQuat BRot(FBarragePrimitive::OptimisticGetAbsoluteRotation(TPrim));
									FVector AMin(JMin.GetX()*100.f, JMin.GetZ()*100.f, JMin.GetY()*100.f);
									FVector AMax(JMax.GetX()*100.f, JMax.GetZ()*100.f, JMax.GetY()*100.f);
									FVector LMin = BRot.UnrotateVector(AMin - BPos);
									FVector LMax = BRot.UnrotateVector(AMax - BPos);
									FVector TMin(FMath::Min(LMin.X,LMax.X), FMath::Min(LMin.Y,LMax.Y), FMath::Min(LMin.Z,LMax.Z));
									FVector TMax(FMath::Max(LMin.X,LMax.X), FMath::Max(LMin.Y,LMax.Y), FMath::Max(LMin.Z,LMax.Z));
									FSurfaceIntegrity NewGrid;
									NewGrid.InitFromAABB(TMin, TMax, TargetPenMat->GridCols, TargetPenMat->GridRows);
									Target.set<FSurfaceIntegrity>(NewGrid);
									Grid = Target.try_get_mut<FSurfaceIntegrity>();
								}
							}
						}
						if (Grid)
						{
							const FBarrageBody* TBody = Target.try_get<FBarrageBody>();
							if (TBody && TBody->IsValid())
							{
								FBLet TPrim = CachedBarrageDispatch->GetShapeRef(TBody->BarrageKey);
								if (FBarragePrimitive::IsNotNull(TPrim))
								{
									FVector BPos(FBarragePrimitive::GetPosition(TPrim));
									FQuat BRot(FBarragePrimitive::OptimisticGetAbsoluteRotation(TPrim));
									int32 CellIdx = Grid->WorldToCell(Pair.ContactPoint, BPos, BRot);
									Grid->DegradeWithSpread(CellIdx, NormDegrade, TargetPenMat->DegradeSpreadFactor);

									// Fragmentation trigger
									if (Grid->GetIntegrity(CellIdx) < 0.05f)
									{
										const FDestructibleStatic* DS = Target.try_get<FDestructibleStatic>();
										if (DS && DS->IsValid() && !Target.has<FPendingFragmentation>())
										{
											FPendingFragmentation Frag;
											Frag.ImpactPoint = Pair.ContactPoint;
											Frag.ImpactDirection = Pair.ContactNormal.IsNearlyZero() ? FVector::UpVector : -Pair.ContactNormal;
											Frag.ImpactImpulse = Damage * 10.f;
											Target.set<FPendingFragmentation>(Frag);
										}
									}
								}
							}
						}
					}
				}
			}

			if (ProjectileEntity.is_valid())
			{
				// Don't kill if this is a spurious re-contact from the same StepWorld as penetration.
				FPenetrationInstance* PenInst = ProjectileEntity.try_get_mut<FPenetrationInstance>();
				if (PenInst && PenInst->LastPenetratedTargetId != 0
					&& (PenInst->LastPenetratedTargetId == TargetId || TargetId == 0))
				{
					PenInst->LastPenetratedTargetId = 0;
					PairEntity.add<FTagCollisionProcessed>();
					return;
				}
				// New target — clear the suppression
				if (PenInst) PenInst->LastPenetratedTargetId = 0;

				bool bIsBouncing = (MaxBounces == -1);
				if (!bIsBouncing)
				{
					FDeathContactPoint DCP;
					DCP.Position = Pair.ContactPoint;
					ProjectileEntity.set<FDeathContactPoint>(DCP);

					// Explosive projectiles: detonate instead of die
					if (ProjectileEntity.try_get<FExplosionStatic>())
					{
						FExplosionContactData ECD;
						ECD.ContactNormal = Pair.ContactNormal;
						ProjectileEntity.set<FExplosionContactData>(ECD);
						ProjectileEntity.add<FTagDetonate>();
					}
					else
					{
						ProjectileEntity.add<FTagDead>();
					}
				}
			}

			PairEntity.add<FTagCollisionProcessed>();
		});

	// ─────────────────────────────────────────────────────────
	// BOUNCE COLLISION SYSTEM
	// ─────────────────────────────────────────────────────────
	World.system<const FCollisionPair>("BounceCollisionSystem")
		.with<FTagCollisionBounce>()
		.without<FTagCollisionProcessed>()
		.each([&World](flecs::entity PairEntity, const FCollisionPair& Pair)
		{
			FVector ContactPoint = Pair.ContactPoint;
			auto ProcessBounce = [&World, ContactPoint](uint64 EntityId, uint64 OtherId) -> bool
			{
				if (EntityId == 0) return false;

				flecs::entity Entity = World.entity(EntityId);
				if (!Entity.is_valid() || Entity.has<FTagDead>()) return false;

				FProjectileInstance* ProjInstance = Entity.try_get_mut<FProjectileInstance>();
				if (!ProjInstance) return false;

				if (ProjInstance->IsOwnedBy(OtherId))
				{
					return true;
				}

				const FProjectileStatic* ProjStatic = Entity.try_get<FProjectileStatic>();
				const int32 MaxBounces = ProjStatic ? ProjStatic->MaxBounces : -1;

				ProjInstance->BounceCount++;

				UE_LOG(LogTemp, Log, TEXT("COLLISION: Bounce %d/%d for Entity %llu"),
					ProjInstance->BounceCount, MaxBounces, EntityId);

				if (MaxBounces >= 0 && ProjInstance->BounceCount > MaxBounces)
				{
					// Don't kill if this is a spurious re-contact from the same StepWorld as penetration.
					// Match: same target entity, OR no-entity contact (OtherId=0) when we just penetrated.
					FPenetrationInstance* PenInst = Entity.try_get_mut<FPenetrationInstance>();
					if (PenInst && PenInst->LastPenetratedTargetId != 0
						&& (PenInst->LastPenetratedTargetId == OtherId || OtherId == 0))
					{
						PenInst->LastPenetratedTargetId = 0;
						ProjInstance->BounceCount = 0;
						return true;
					}
					// New target — clear the suppression
					if (PenInst) PenInst->LastPenetratedTargetId = 0;

					FDeathContactPoint DCP;
					DCP.Position = ContactPoint;
					Entity.set<FDeathContactPoint>(DCP);
					Entity.add<FTagDead>();
					UE_LOG(LogTemp, Log, TEXT("COLLISION: Projectile %llu exceeded max bounces at (%.0f,%.0f,%.0f)"),
						EntityId, ContactPoint.X, ContactPoint.Y, ContactPoint.Z);
				}

				return true;
			};

			if (!ProcessBounce(Pair.EntityId1, Pair.EntityId2))
			{
				ProcessBounce(Pair.EntityId2, Pair.EntityId1);
			}

			PairEntity.add<FTagCollisionProcessed>();
		});
}
