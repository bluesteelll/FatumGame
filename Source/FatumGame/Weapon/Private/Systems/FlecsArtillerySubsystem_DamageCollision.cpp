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
#include "Library/FlecsBulletHitLibrary.h"

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
			const uint64 ProjectileId = Pair.GetProjectileEntityId();
			const uint64 TargetId = Pair.GetTargetEntityId();

			flecs::entity ProjectileEntity;
			int32 MaxBounces = 0;

			if (ProjectileId != 0)
			{
				ProjectileEntity = World.entity(ProjectileId);
				if (ProjectileEntity.is_valid())
				{
					if (const FProjectileStatic* PS = ProjectileEntity.try_get<FProjectileStatic>())
						MaxBounces = PS->MaxBounces;
				}
			}

			// ── Resolve target and delegate damage / degradation / fragmentation to shared library ──
			if (TargetId != 0 && ProjectileEntity.is_valid())
			{
				flecs::entity Target = World.entity(TargetId);
				if (Target.is_valid() && !Target.has<FTagDead>())
				{
					const FDamageStatic* DmgStatic = ProjectileEntity.try_get<FDamageStatic>();
					const FProjectileStatic* ProjStatic = ProjectileEntity.try_get<FProjectileStatic>();
					const FProjectileInstance* ProjInst = ProjectileEntity.try_get<FProjectileInstance>();
					const FPenetrationInstance* PenInst = ProjectileEntity.try_get<FPenetrationInstance>();

					FBulletHitContext Ctx;
					Ctx.ShooterEntityId   = ProjInst ? static_cast<uint64>(ProjInst->OwnerEntityId) : 0;
					Ctx.TargetEntityId    = TargetId;
					Ctx.ImpactPoint       = Pair.ContactPoint;
					Ctx.ImpactNormal      = Pair.ContactNormal;
					Ctx.IncomingDir       = Pair.IncomingVelocity.GetSafeNormal();
					Ctx.IncomingSpeed     = static_cast<float>(Pair.IncomingVelocity.Size());
					Ctx.SpawnPosition     = ProjInst ? ProjInst->SpawnPosition : FVector::ZeroVector;
					Ctx.BaseDamage        = DmgStatic ? DmgStatic->Damage : 25.f;
					Ctx.StructuralDamage  = DmgStatic ? DmgStatic->StructuralDamage : 0.f;
					Ctx.CritChance        = DmgStatic ? DmgStatic->CritChance : 0.f;
					Ctx.CritMultiplier    = DmgStatic ? DmgStatic->CritMultiplier : 2.f;
					if (DmgStatic) Ctx.DamageType = DmgStatic->DamageType;

					if (ProjStatic)
					{
						Ctx.DamageFalloffStart  = ProjStatic->DamageFalloffStart;
						Ctx.InvFalloffRange     = ProjStatic->InvFalloffRange;
						Ctx.MinDamageMultiplier = ProjStatic->MinDamageMultiplier;
					}

					// Seed in-out damage multiplier from penetration instance so the library
					// applies post-penetration falloff. All other pen in-out pointers stay null
					// — we don't want the library to decide to penetrate here.
					float LocalDmgMul = PenInst ? PenInst->CurrentDamageMultiplier : 1.f;
					Ctx.InOutCurrentDamageMultiplier = PenInst ? &LocalDmgMul : nullptr;
					// Leave PenStatic == nullptr → library will not enter the penetration branch.
					Ctx.bApplyImpulse = false;
					Ctx.bCanDegrade   = true;

					UFlecsBulletHitLibrary::ApplyBulletHit(
						World, CachedBarrageDispatch, ProjectileEntity, Target, Ctx);
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

				const bool bIsBouncing = (MaxBounces == -1);
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
