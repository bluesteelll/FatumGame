// FlecsArtillerySubsystem - Bidirectional Binding API + Damage System API

#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FlecsGameTags.h"
#include "FlecsInstanceComponents.h"
#include "FlecsEntityDefinition.h"
#include "FlecsProjectileProfile.h"
#include "BarrageSpawnUtils.h"
#include "FlecsRenderManager.h"

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
	// Must be called on Game thread.
	// Physics body + Flecs entity are created on sim thread (WeaponFireSystem).
	// This only registers the ISM render instance (UE rendering requires game thread).
	check(IsInGameThread());

	UFlecsRenderManager* Renderer = GetRenderManager();
	if (!Renderer) return;

	FPendingProjectileSpawn Spawn;
	while (PendingProjectileSpawns.Dequeue(Spawn))
	{
		if (!Spawn.Mesh || !Spawn.EntityKey.IsValid()) continue;

		FQuat DirQuat = FRotationMatrix::MakeFromX(Spawn.Direction).ToQuat();

		FTransform RenderTransform;
		RenderTransform.SetLocation(Spawn.Location);
		RenderTransform.SetRotation(DirQuat * Spawn.RotationOffset.Quaternion());
		RenderTransform.SetScale3D(Spawn.Scale);

		Renderer->AddInstance(Spawn.Mesh, Spawn.Material, RenderTransform, Spawn.EntityKey);
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
