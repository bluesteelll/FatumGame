// FlecsArtillerySubsystem - Damage & Bounce Collision Systems (Weapon domain)

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsBarrageComponents.h"
#include "FlecsHealthComponents.h"
#include "FlecsProjectileComponents.h"
#include "FlecsExplosionComponents.h"
#include "FlecsPenetrationComponents.h"
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
		.each([&World](flecs::entity PairEntity, const FCollisionPair& Pair)
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
						bool bIsCritical = (CritChance > 0.f && FMath::FRand() < CritChance);
						FPendingDamage& Pending = Target.obtain<FPendingDamage>();
						Pending.AddHit(Damage, ProjectileId, DamageType, Pair.ContactPoint, bIsCritical, false);
						Target.modified<FPendingDamage>();

						UE_LOG(LogTemp, Log, TEXT("COLLISION: Queued %.1f damage to Entity %llu (Crit=%d)"),
							Damage, TargetId, bIsCritical);
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
