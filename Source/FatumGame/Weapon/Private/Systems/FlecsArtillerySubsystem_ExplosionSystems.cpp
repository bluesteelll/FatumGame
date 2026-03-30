// FlecsArtillerySubsystem - Explosion Systems
// Processes FTagDetonate entities: applies radial damage + impulse, spawns VFX, transitions to FTagDead.

#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FlecsBarrageComponents.h"
#include "FlecsExplosionComponents.h"
#include "FlecsProjectileComponents.h"
#include "FlecsHealthComponents.h"
#include "FlecsGameTags.h"
#include "FlecsNiagaraManager.h"
#include "ExplosionUtility.h"

void UFlecsArtillerySubsystem::SetupExplosionSystems()
{
	flecs::world& World = *FlecsWorld;

	// ─────────────────────────────────────────────────────────
	// EXPLOSION SYSTEM
	// Processes all entities tagged FTagDetonate this tick.
	// Reads FExplosionStatic (prefab) + body position → builds FExplosionParams → ApplyExplosion.
	// Enqueues VFX, then transitions FTagDetonate → FTagDead.
	// ─────────────────────────────────────────────────────────
	World.system<const FExplosionStatic, const FBarrageBody>("ExplosionSystem")
		.with<FTagDetonate>()
		.without<FTagDead>()
		.each([this, &World](flecs::entity Entity, const FExplosionStatic& ExplStatic, const FBarrageBody& Body)
		{
			EnsureBarrageAccess();

			// ── Determine epicenter ──
			FVector Epicenter = FVector::ZeroVector;
			bool bHasPosition = false;

			// Try physics body position first
			if (Body.IsValid() && CachedBarrageDispatch)
			{
				FBLet Prim = CachedBarrageDispatch->GetShapeRef(Body.BarrageKey);
				if (FBarragePrimitive::IsNotNull(Prim))
				{
					const FVector3f PosF = FBarragePrimitive::GetPosition(Prim);
					Epicenter = FVector(PosF.X, PosF.Y, PosF.Z);
					bHasPosition = true;
				}
			}

			// Fallback: stored death contact point (set by collision system)
			if (!bHasPosition)
			{
				const FDeathContactPoint* DCP = Entity.try_get<FDeathContactPoint>();
				if (DCP && !DCP->Position.IsNearlyZero())
				{
					Epicenter = DCP->Position;
					bHasPosition = true;
				}
			}

			if (!bHasPosition)
			{
				UE_LOG(LogTemp, Warning, TEXT("EXPLOSION: Entity %llu has no valid position, skipping blast"),
					Entity.id());
				Entity.add<FTagDead>();
				Entity.remove<FTagDetonate>();
				return;
			}

			// ── Apply epicenter lift along contact normal ──
			const FExplosionContactData* ECD = Entity.try_get<FExplosionContactData>();
			FVector LiftDir = (ECD && !ECD->ContactNormal.IsNearlyZero())
				? ECD->ContactNormal.GetSafeNormal()
				: FVector::UpVector;
			Epicenter += LiftDir * ExplStatic.EpicenterLift;

			// ── Build explosion params ──
			FExplosionParams Params;
			Params.EpicenterUE = Epicenter;
			Params.Radius = ExplStatic.Radius;
			Params.BaseDamage = ExplStatic.BaseDamage;
			Params.ImpulseStrength = ExplStatic.ImpulseStrength;
			Params.DamageFalloff = ExplStatic.DamageFalloff;
			Params.ImpulseFalloff = ExplStatic.ImpulseFalloff;
			Params.VerticalBias = ExplStatic.VerticalBias;
			Params.bDamageOwner = ExplStatic.bDamageOwner;
			Params.DamageType = ExplStatic.DamageType;

			// Owner from projectile instance (if this is a projectile)
			const FProjectileInstance* ProjInst = Entity.try_get<FProjectileInstance>();
			Params.OwnerEntityId = ProjInst ? static_cast<uint64>(ProjInst->OwnerEntityId) : 0;

			// ── Fire explosion ──
			ApplyExplosion(Params, CachedBarrageDispatch, CharacterBridges, World);

			// ── Queue explosion VFX ──
			UNiagaraSystem* VFX = ExplStatic.ExplosionEffect;
			float VFXScale = ExplStatic.ExplosionEffectScale;

			// Fallback to entity's death VFX if no explosion-specific one
			if (!VFX)
			{
				const FNiagaraDeathEffect* DeathVFX = Entity.try_get<FNiagaraDeathEffect>();
				if (DeathVFX && DeathVFX->Effect)
				{
					VFX = DeathVFX->Effect;
					VFXScale = DeathVFX->Scale;
				}
			}

			if (VFX)
			{
				if (UFlecsNiagaraManager* NiagaraMgr = UFlecsNiagaraManager::Get(GetWorld()))
				{
					FPendingDeathEffect FX;
					FX.Location = Epicenter;
					FX.Effect = VFX;
					FX.Scale = VFXScale;
					NiagaraMgr->EnqueueDeathEffect(FX);
				}
			}

			UE_LOG(LogTemp, Log, TEXT("EXPLOSION: Entity %llu detonated at (%.0f,%.0f,%.0f) R=%.0f Dmg=%.1f Imp=%.0f"),
				Entity.id(), Epicenter.X, Epicenter.Y, Epicenter.Z,
				ExplStatic.Radius, ExplStatic.BaseDamage, ExplStatic.ImpulseStrength);

			// ── Transition: detonate → dead ──
			Entity.add<FTagDead>();
			Entity.remove<FTagDetonate>();
		});
}
