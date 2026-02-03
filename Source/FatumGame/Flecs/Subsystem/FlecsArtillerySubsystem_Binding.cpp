// FlecsArtillerySubsystem - Bidirectional Binding API

#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FlecsGameTags.h"
#include "Systems/BarrageEntitySpawner.h"

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
	Entity.set<FBarrageBody>({ BarrageKey });

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

UBarrageRenderManager* UFlecsArtillerySubsystem::GetRenderManager() const
{
	if (CachedBarrageDispatch && CachedBarrageDispatch->GetWorld())
	{
		return UBarrageRenderManager::Get(CachedBarrageDispatch->GetWorld());
	}
	return nullptr;
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
