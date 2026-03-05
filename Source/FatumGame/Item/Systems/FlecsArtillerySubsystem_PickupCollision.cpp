// FlecsArtillerySubsystem - Pickup Collision System (Item domain)

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsBarrageComponents.h"
#include "FlecsItemComponents.h"

void UFlecsArtillerySubsystem::SetupPickupCollisionSystems()
{
	flecs::world& World = *FlecsWorld;

	// ─────────────────────────────────────────────────────────
	// PICKUP COLLISION SYSTEM
	// ─────────────────────────────────────────────────────────
	World.system<const FCollisionPair>("PickupCollisionSystem")
		.with<FTagCollisionPickup>()
		.without<FTagCollisionProcessed>()
		.each([&World](flecs::entity PairEntity, const FCollisionPair& Pair)
		{
			flecs::entity Entity1 = Pair.HasEntity1() ? World.entity(Pair.EntityId1) : flecs::entity();
			flecs::entity Entity2 = Pair.HasEntity2() ? World.entity(Pair.EntityId2) : flecs::entity();

			flecs::entity Character;
			flecs::entity Item;

			if (Entity1.is_valid() && Entity1.has<FTagCharacter>())
			{
				Character = Entity1;
				Item = Entity2;
			}
			else if (Entity2.is_valid() && Entity2.has<FTagCharacter>())
			{
				Character = Entity2;
				Item = Entity1;
			}

			if (Character.is_valid() && Item.is_valid() && !Item.has<FTagDead>())
			{
				const FWorldItemInstance* WorldItem = Item.try_get<FWorldItemInstance>();
				if (WorldItem && !WorldItem->CanBePickedUp())
				{
					PairEntity.add<FTagCollisionProcessed>();
					return;
				}

				UE_LOG(LogTemp, Log, TEXT("COLLISION: Pickup triggered - Character %llu touching Item %llu"),
					Character.id(), Item.id());

				// TODO: Transfer item to character's inventory
			}

			PairEntity.add<FTagCollisionProcessed>();
		});
}
