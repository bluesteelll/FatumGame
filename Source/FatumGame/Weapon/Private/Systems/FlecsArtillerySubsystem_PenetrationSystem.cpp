// FlecsArtillerySubsystem - Penetration System
// Processes FTagCollisionPenetration pairs BEFORE DamageCollisionSystem.
// All shared logic (slab thickness, integrity grid, damage, degradation, fragmentation)
// lives in UFlecsBulletHitLibrary::ApplyBulletHit. This system only handles projectile-
// specific plumbing: validation gates, context build, body teleport/velocity update.

#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FlecsBarrageComponents.h"
#include "FlecsPenetrationComponents.h"
#include "FlecsProjectileComponents.h"
#include "FlecsHealthComponents.h"
#include "FlecsExplosionComponents.h"
#include "FlecsGameTags.h"
#include "Library/FlecsBulletHitLibrary.h"

void UFlecsArtillerySubsystem::SetupPenetrationSystem()
{
	flecs::world& World = *FlecsWorld;

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

			// ── Incoming velocity (captured pre-collision in contact listener) ──
			const FVector IncomingVelocity = Pair.IncomingVelocity;
			const float IncomingSpeed = static_cast<float>(IncomingVelocity.Size());
			if (IncomingSpeed < 1.f) return;  // Stopped

			const FVector IncomingDir = IncomingVelocity / IncomingSpeed;

			// ── Get projectile physics body (needed for teleport + velocity update) ──
			const FBarrageBody* ProjBody = ProjEntity.try_get<FBarrageBody>();
			if (!ProjBody || !ProjBody->IsValid() || !CachedBarrageDispatch) return;

			FBLet ProjPrim = CachedBarrageDispatch->GetShapeRef(ProjBody->BarrageKey);
			if (!FBarragePrimitive::IsNotNull(ProjPrim)) return;

			// ── Build context & delegate to shared library ──
			const FDamageStatic* DmgStatic = ProjEntity.try_get<FDamageStatic>();
			const FProjectileStatic* ProjStatic = ProjEntity.try_get<FProjectileStatic>();
			const FProjectileInstance* ProjInst = ProjEntity.try_get<FProjectileInstance>();

			FBulletHitContext Ctx;
			Ctx.ShooterEntityId   = ProjInst ? static_cast<uint64>(ProjInst->OwnerEntityId) : 0;
			Ctx.TargetEntityId    = TargetId;
			Ctx.ImpactPoint       = Pair.ContactPoint;
			Ctx.ImpactNormal      = Pair.ContactNormal;
			Ctx.IncomingDir       = IncomingDir;
			Ctx.SpawnPosition     = ProjInst ? ProjInst->SpawnPosition : FVector::ZeroVector;
			Ctx.IncomingSpeed     = IncomingSpeed;
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

			Ctx.SubShapeIDValue = Pair.SubShapeID2;
			Ctx.PenStatic       = PenStatic;
			Ctx.InOutRemainingBudget         = &PenInstance->RemainingBudget;
			Ctx.InOutCurrentDamageMultiplier = &PenInstance->CurrentDamageMultiplier;
			Ctx.InOutPenetrationCount        = &PenInstance->PenetrationCount;
			Ctx.InOutLastPenetratedTargetId  = &PenInstance->LastPenetratedTargetId;
			Ctx.bApplyImpulse = false;
			Ctx.bCanDegrade   = true;

			const FBulletHitResult Res = UFlecsBulletHitLibrary::ApplyBulletHit(
				World, CachedBarrageDispatch, ProjEntity, TargetEntity, Ctx);

			if (!Res.bPenetrated)
			{
				// Non-penetrating path: DamageCollisionSystem will apply the main kill/damage.
				// Library already handled surface degradation + fragmentation trigger.
				return;
			}

			// ════════════════════════════════════════════════════════
			// PENETRATE: teleport body past exit surface and reduce velocity
			// ════════════════════════════════════════════════════════

			// Velocity multiplier: derive from budget consumed this hit.
			// (Library updated RemainingBudget already; recompute fraction from delta for clarity.)
			const float BudgetFractionThisHit = FMath::Clamp(
				(PenStatic->PenetrationBudget > 0.01f)
					? ((PenStatic->PenetrationBudget - PenInstance->RemainingBudget) / PenStatic->PenetrationBudget)
					: 0.f,
				0.f, 1.f);
			const float VelocityMultiplier = FMath::Max(0.1f,
				1.f - BudgetFractionThisHit * PenStatic->VelocityFalloffFactor);

			const FVector ExitLocation = Res.ExitPoint + IncomingDir * 5.f;  // 5cm past exit to clear surface
			const FBarrageKey ProjBarrageKey = ProjPrim->KeyIntoBarrage;
			CachedBarrageDispatch->SetBodyPositionDirect(ProjBarrageKey, ExitLocation);

			const FVector NewVelocity = IncomingDir * IncomingSpeed * VelocityMultiplier;
			CachedBarrageDispatch->SetBodyLinearVelocityDirect(ProjBarrageKey, NewVelocity);

			PairEntity.add<FTagCollisionProcessed>();

			UE_LOG(LogTemp, Log,
				TEXT("PENETRATION: Projectile %llu through target %llu Budget=%.1f DmgMult=%.2f VelMult=%.2f Count=%d"),
				ProjectileId, TargetId,
				PenInstance->RemainingBudget,
				PenInstance->CurrentDamageMultiplier,
				VelocityMultiplier,
				PenInstance->PenetrationCount);
		});
}
