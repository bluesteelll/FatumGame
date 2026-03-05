// FlecsArtillerySubsystem - Bidirectional Binding API + Damage System API

#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FlecsGameTags.h"
#include "FlecsBarrageComponents.h"
#include "FlecsHealthComponents.h"
#include "FlecsEntityDefinition.h"
#include "FlecsProjectileProfile.h"
#include "BarrageSpawnUtils.h"
#include "FlecsRenderManager.h"
#include "FlecsNiagaraManager.h"

// ═══════════════════════════════════════════════════════════════
// BIDIRECTIONAL BINDING API (Lock-Free)
// ═══════════════════════════════════════════════════════════════

flecs::world UFlecsArtillerySubsystem::GetStage(int32 ThreadIndex) const
{
	check(FlecsWorld);
	// For now, always return main world (stage 0).
	// Future: create and return thread-specific stages for parallel collision processing.
	return FlecsWorld->get_stage(FMath::Clamp(ThreadIndex, 0, MaxStages - 1));
}

void UFlecsArtillerySubsystem::BindEntityToBarrage(flecs::entity Entity, FSkeletonKey BarrageKey)
{
	if (!Entity.is_valid() || !BarrageKey.IsValid()) return;

	// Forward binding: set FBarrageBody component on Flecs entity
	// NOTE: Cannot use aggregate init { BarrageKey } with USTRUCT that has GENERATED_BODY()
	FBarrageBody Body;
	Body.BarrageKey = BarrageKey;
	Entity.set<FBarrageBody>(Body);

	// Reverse binding: store Flecs entity ID in FBarragePrimitive (atomic)
	if (CachedBarrageDispatch)
	{
		FBLet Prim = CachedBarrageDispatch->GetShapeRef(BarrageKey);
		if (FBarragePrimitive::IsNotNull(Prim))
		{
			Prim->SetFlecsEntity(Entity.id());
		}
	}
}

void UFlecsArtillerySubsystem::UnbindEntityFromBarrage(flecs::entity Entity)
{
	if (!Entity.is_valid()) return;

	// Get BarrageKey from forward binding before removing it
	const FBarrageBody* Body = Entity.try_get<FBarrageBody>();
	if (Body && Body->IsValid())
	{
		FSkeletonKey Key = Body->BarrageKey;

		// Clear reverse binding (atomic in FBarragePrimitive)
		if (CachedBarrageDispatch)
		{
			FBLet Prim = CachedBarrageDispatch->GetShapeRef(Key);
			if (FBarragePrimitive::IsNotNull(Prim))
			{
				Prim->ClearFlecsEntity();
			}
		}
	}

	// Clear forward binding by removing component
	Entity.remove<FBarrageBody>();
}

flecs::entity UFlecsArtillerySubsystem::GetEntityForBarrageKey(FSkeletonKey BarrageKey) const
{
	if (!FlecsWorld || !BarrageKey.IsValid()) return flecs::entity();

	// Lock-free O(1) lookup: libcuckoo → FBLet → atomic load
	if (CachedBarrageDispatch)
	{
		FBLet Prim = CachedBarrageDispatch->GetShapeRef(BarrageKey);
		if (FBarragePrimitive::IsNotNull(Prim))
		{
			uint64 FlecsId = Prim->GetFlecsEntity();
			if (FlecsId != 0)
			{
				return FlecsWorld->entity(FlecsId);
			}
		}
	}
	return flecs::entity();
}

FSkeletonKey UFlecsArtillerySubsystem::GetBarrageKeyForEntity(flecs::entity Entity) const
{
	if (!Entity.is_valid()) return FSkeletonKey();

	// O(1) lookup via Flecs sparse set
	const FBarrageBody* Body = Entity.try_get<FBarrageBody>();
	return Body ? Body->BarrageKey : FSkeletonKey();
}

bool UFlecsArtillerySubsystem::HasEntityForBarrageKey(FSkeletonKey BarrageKey) const
{
	if (!BarrageKey.IsValid()) return false;

	// Lock-free O(1) check: libcuckoo → FBLet → atomic load
	if (CachedBarrageDispatch)
	{
		FBLet Prim = CachedBarrageDispatch->GetShapeRef(BarrageKey);
		if (FBarragePrimitive::IsNotNull(Prim))
		{
			return Prim->HasFlecsEntity();
		}
	}
	return false;
}

UFlecsRenderManager* UFlecsArtillerySubsystem::GetRenderManager() const
{
	if (CachedBarrageDispatch && CachedBarrageDispatch->GetWorld())
	{
		return UFlecsRenderManager::Get(CachedBarrageDispatch->GetWorld());
	}
	return nullptr;
}

UFlecsNiagaraManager* UFlecsArtillerySubsystem::GetNiagaraManager() const
{
	if (CachedBarrageDispatch && CachedBarrageDispatch->GetWorld())
	{
		return UFlecsNiagaraManager::Get(CachedBarrageDispatch->GetWorld());
	}
	return nullptr;
}

// ═══════════════════════════════════════════════════════════════
// DAMAGE SYSTEM API
// ═══════════════════════════════════════════════════════════════

bool UFlecsArtillerySubsystem::QueueDamage(flecs::entity Target, float Damage, uint64 SourceEntityId,
                                            FGameplayTag DamageType, FVector HitLocation, bool bIgnoreArmor)
{
	if (!Target.is_valid() || !FlecsWorld)
	{
		return false;
	}

	// Target must have health to receive damage
	if (!Target.has<FHealthInstance>())
	{
		UE_LOG(LogTemp, Warning, TEXT("QueueDamage: Entity %llu has no FHealthInstance"), Target.id());
		return false;
	}

	// Don't damage dead entities
	if (Target.has<FTagDead>())
	{
		return false;
	}

	// Get or add FPendingDamage component - obtain() adds if missing and returns reference
	FPendingDamage& Pending = Target.obtain<FPendingDamage>();

	// Queue the damage hit
	Pending.AddHit(Damage, SourceEntityId, DamageType, HitLocation, false, bIgnoreArmor);

	// Trigger the observer
	Target.modified<FPendingDamage>();

	UE_LOG(LogTemp, Verbose, TEXT("QueueDamage: %.1f damage queued to Entity %llu from %llu"),
		Damage, Target.id(), SourceEntityId);

	return true;
}

bool UFlecsArtillerySubsystem::QueueDamageByKey(FSkeletonKey TargetKey, float Damage, uint64 SourceEntityId,
                                                 FGameplayTag DamageType)
{
	flecs::entity Target = GetEntityForBarrageKey(TargetKey);
	if (!Target.is_valid())
	{
		UE_LOG(LogTemp, Warning, TEXT("QueueDamageByKey: No entity for key %llu"), static_cast<uint64>(TargetKey));
		return false;
	}

	return QueueDamage(Target, Damage, SourceEntityId, DamageType);
}

// ═══════════════════════════════════════════════════════════════
// WEAPON SYSTEM - PROJECTILE SPAWNING
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::ProcessPendingProjectileSpawns()
{
	// Inverted flow: game thread reads FRESH muzzle world position from bridge atomics
	// (follows weapon mesh socket animation). Falls back to sim-computed position.
	//
	// Ordering guarantee (in Tick):
	//   1. UpdateTransforms(Alpha, SimTick)   — interpolates existing ISMs
	//   2. ProcessPendingProjectileSpawns()    — adds new ISMs at correct game-thread positions
	check(IsInGameThread());

	UFlecsRenderManager* Renderer = GetRenderManager();
	if (!Renderer) return;

	// Read CURRENT muzzle position from bridge atomics (written by AFlecsCharacter::Tick every frame)
	FVector FreshMuzzle = FVector::ZeroVector;
	bool bHasFreshMuzzle = false;
	if (LateSyncBridge)
	{
		bHasFreshMuzzle = LateSyncBridge->WriteSeqNum.load(std::memory_order_relaxed) > 0;
		if (bHasFreshMuzzle)
		{
			FreshMuzzle = FVector(
				LateSyncBridge->LastWrittenMuzzleX.load(std::memory_order_relaxed),
				LateSyncBridge->LastWrittenMuzzleY.load(std::memory_order_relaxed),
				LateSyncBridge->LastWrittenMuzzleZ.load(std::memory_order_relaxed));
			// Zero muzzle means no weapon socket — fall back to sim-computed
			if (FreshMuzzle.IsNearlyZero())
			{
				bHasFreshMuzzle = false;
			}
		}
	}

	FPendingProjectileSpawn Spawn;
	while (PendingProjectileSpawns.Dequeue(Spawn))
	{
		if (!Spawn.Mesh || !Spawn.EntityKey.IsValid()) continue;

		// Use fresh muzzle position (animated socket), or fall back to sim-computed
		FVector SpawnLocation = bHasFreshMuzzle ? FreshMuzzle : Spawn.SimComputedLocation;

		FQuat DirQuat = FRotationMatrix::MakeFromX(Spawn.SpawnDirection).ToQuat();

		FTransform RenderTransform;
		RenderTransform.SetLocation(SpawnLocation);
		RenderTransform.SetRotation(DirQuat * Spawn.RotationOffset.Quaternion());
		RenderTransform.SetScale3D(Spawn.Scale);

		Renderer->AddInstance(Spawn.Mesh, Spawn.Material, RenderTransform, Spawn.EntityKey);

		// Register attached Niagara VFX for this projectile
		if (Spawn.NiagaraEffect)
		{
			if (UFlecsNiagaraManager* NiagaraMgr = GetNiagaraManager())
			{
				NiagaraMgr->RegisterEntity(Spawn.EntityKey, Spawn.NiagaraEffect,
					Spawn.NiagaraScale, Spawn.NiagaraOffset);
			}
		}

		UE_LOG(LogTemp, Log, TEXT("INTERP [Spawn] Key=%llu FreshMuzzle=%d Correction=%.2f SpawnLoc=(%.1f,%.1f,%.1f) SimLoc=(%.1f,%.1f,%.1f)"),
			static_cast<uint64>(Spawn.EntityKey),
			bHasFreshMuzzle, FVector::Dist(SpawnLocation, Spawn.SimComputedLocation),
			SpawnLocation.X, SpawnLocation.Y, SpawnLocation.Z,
			Spawn.SimComputedLocation.X, Spawn.SimComputedLocation.Y, Spawn.SimComputedLocation.Z);

		// NOTE: Do NOT correct physics body position here.
		// The body was created at SimMuzzle with velocity computed from SimMuzzle.
		// SetPosition(GameMuzzle) would break position↔velocity consistency:
		// body at GameMuzzle but velocity direction computed from SimMuzzle → wrong trajectory.
		// ISM starts at GameMuzzle (1 frame visual), then syncs to physics via interpolation.
		// At 5000+ u/s the 2-5u sim↔game muzzle difference is invisible.
	}
}

// ═══════════════════════════════════════════════════════════════
// DEPRECATED API (backward compatibility during migration)
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::RegisterBarrageEntity(FSkeletonKey BarrageKey, uint64 FlecsEntityId)
{
	// Deprecated: Use BindEntityToBarrage instead.
	if (CachedBarrageDispatch && BarrageKey.IsValid())
	{
		FBLet Prim = CachedBarrageDispatch->GetShapeRef(BarrageKey);
		if (FBarragePrimitive::IsNotNull(Prim))
		{
			Prim->SetFlecsEntity(FlecsEntityId);
		}
	}
}

void UFlecsArtillerySubsystem::UnregisterBarrageEntity(FSkeletonKey BarrageKey)
{
	// Deprecated: Use UnbindEntityFromBarrage instead.
	if (CachedBarrageDispatch && BarrageKey.IsValid())
	{
		FBLet Prim = CachedBarrageDispatch->GetShapeRef(BarrageKey);
		if (FBarragePrimitive::IsNotNull(Prim))
		{
			Prim->ClearFlecsEntity();
		}
	}
}

uint64 UFlecsArtillerySubsystem::GetFlecsEntityForBarrageKey(FSkeletonKey BarrageKey) const
{
	// Deprecated: Use GetEntityForBarrageKey instead.
	flecs::entity Entity = GetEntityForBarrageKey(BarrageKey);
	return Entity.is_valid() ? Entity.id() : 0;
}

bool UFlecsArtillerySubsystem::HasFlecsEntityForBarrageKey(FSkeletonKey BarrageKey) const
{
	// Deprecated: Use HasEntityForBarrageKey instead.
	return HasEntityForBarrageKey(BarrageKey);
}
