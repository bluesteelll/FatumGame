// FlecsArtillerySubsystem - Destructible Collision System (Destructible domain)

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsBarrageComponents.h"

void UFlecsArtillerySubsystem::SetupDestructibleCollisionSystems()
{
	flecs::world& World = *FlecsWorld;

	// ─────────────────────────────────────────────────────────
	// DESTRUCTIBLE COLLISION SYSTEM
	// ─────────────────────────────────────────────────────────
	World.system<const FCollisionPair>("DestructibleCollisionSystem")
		.with<FTagCollisionDestructible>()
		.without<FTagCollisionProcessed>()
		.each([&World](flecs::entity PairEntity, const FCollisionPair& Pair)
		{
			auto TryDestroyDestructible = [&World](uint64 EntityId) -> bool
			{
				if (EntityId == 0) return false;

				flecs::entity Entity = World.entity(EntityId);
				if (!Entity.is_valid() || Entity.has<FTagDead>()) return false;

				if (Entity.has<FTagDestructible>())
				{
					Entity.add<FTagDead>();
					UE_LOG(LogTemp, Log, TEXT("COLLISION: Destructible %llu destroyed"), EntityId);
					return true;
				}
				return false;
			};

			TryDestroyDestructible(Pair.EntityId1);
			TryDestroyDestructible(Pair.EntityId2);

			PairEntity.add<FTagCollisionProcessed>();
		});
}
