#include "FLateSyncBridge.h"
#include "FlecsWeaponComponents.h"
#include "flecs.h"

void FLateSyncBridge::WriteAimDirection(int64 CharacterEntityId, const FAimDirection& AimDir)
{
	AimCharacterEntityId.store(CharacterEntityId, std::memory_order_relaxed);
	AimBuffer.WriteAndSwap(AimDir);

	WriteSeqNum.fetch_add(1, std::memory_order_relaxed);
	LastWrittenX.store(static_cast<float>(AimDir.CharacterPosition.X), std::memory_order_relaxed);
	LastWrittenY.store(static_cast<float>(AimDir.CharacterPosition.Y), std::memory_order_relaxed);
	LastWrittenZ.store(static_cast<float>(AimDir.CharacterPosition.Z), std::memory_order_relaxed);
	LastWrittenMuzzleX.store(static_cast<float>(AimDir.MuzzleWorldPosition.X), std::memory_order_relaxed);
	LastWrittenMuzzleY.store(static_cast<float>(AimDir.MuzzleWorldPosition.Y), std::memory_order_relaxed);
	LastWrittenMuzzleZ.store(static_cast<float>(AimDir.MuzzleWorldPosition.Z), std::memory_order_relaxed);
}

void FLateSyncBridge::ApplyAll(flecs::world* World)
{
	if (!World || !AimBuffer.IsDirty()) return;

	int64 CharId = AimCharacterEntityId.load(std::memory_order_relaxed);
	if (CharId == 0) return;

	const FAimDirection& Aim = AimBuffer.SwapAndRead();
	ReadSeqNum.fetch_add(1, std::memory_order_relaxed);

	flecs::entity CharEntity = World->entity(static_cast<flecs::entity_t>(CharId));
	if (CharEntity.is_valid() && CharEntity.is_alive())
	{
		CharEntity.set<FAimDirection>(Aim);
	}
}
