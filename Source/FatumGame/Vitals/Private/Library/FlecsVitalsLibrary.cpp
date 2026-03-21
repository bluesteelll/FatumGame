// Vitals item consumption — enqueues to sim thread.

#include "FlecsVitalsLibrary.h"
#include "FlecsArtillerySubsystem.h"
#include "FlecsVitalsComponents.h"
#include "FlecsItemComponents.h"
#include "FlecsGameTags.h"
#include "flecs.h"

void UFlecsVitalsLibrary::ConsumeVitalItem(UObject* WorldContextObject, int64 ItemEntityId, int64 CharacterEntityId)
{
	if (ItemEntityId == 0 || CharacterEntityId == 0) return;

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!World) return;

	UFlecsArtillerySubsystem* Subsystem = World->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!Subsystem) return;

	Subsystem->EnqueueCommand([Subsystem, ItemEntityId, CharacterEntityId]()
	{
		flecs::world* FlecsWorld = Subsystem->GetFlecsWorld();
		if (!FlecsWorld) return;

		flecs::entity ItemEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(ItemEntityId));
		flecs::entity CharEntity = FlecsWorld->entity(static_cast<flecs::entity_t>(CharacterEntityId));

		if (!ItemEntity.is_valid() || !ItemEntity.is_alive()) return;
		if (!CharEntity.is_valid() || !CharEntity.is_alive()) return;

		// Read vitals restoration data from item prefab
		const FVitalsItemStatic* VIS = ItemEntity.try_get<FVitalsItemStatic>();
		if (!VIS || !VIS->HasConsumableEffect()) return;

		// Apply restoration to character vitals
		FVitalsInstance* VI = CharEntity.try_get_mut<FVitalsInstance>();
		if (!VI) return;

		VI->HungerPercent = FMath::Min(1.f, VI->HungerPercent + VIS->HungerRestore);
		VI->ThirstPercent = FMath::Min(1.f, VI->ThirstPercent + VIS->ThirstRestore);
		VI->WarmthPercent = FMath::Min(1.f, VI->WarmthPercent + VIS->WarmthRestore);

		// Decrement item count
		FItemInstance* ItemInst = ItemEntity.try_get_mut<FItemInstance>();
		if (ItemInst)
		{
			ItemInst->Count -= 1;
			if (ItemInst->Count <= 0)
			{
				ItemEntity.add<FTagDead>();
			}
		}

		// Mark equipment dirty (item count changed or removed)
		VI->bEquipmentDirty = true;

		// Update SimStateCache immediately
		const int64 EntityId = static_cast<int64>(CharEntity.id());
		Subsystem->GetSimStateCache().WriteVitals(EntityId, VI->HungerPercent, VI->ThirstPercent, VI->WarmthPercent);
	});
}
