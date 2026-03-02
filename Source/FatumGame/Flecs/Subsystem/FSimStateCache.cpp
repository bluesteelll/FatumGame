
#include "FSimStateCache.h"

FSimStateCache::FSimStateCache()
{
	for (int32 i = 0; i < MaxSlots; ++i)
	{
		EntityIds[i].store(0, std::memory_order_relaxed);
		HealthPacked[i].store(0, std::memory_order_relaxed);
		WeaponPacked[i].store(0, std::memory_order_relaxed);
	}
}

// ═══════════════════════════════════════════════════════════════
// SIM THREAD: REGISTRATION
// ═══════════════════════════════════════════════════════════════

int32 FSimStateCache::Register(int64 EntityId)
{
	if (EntityId == 0) return -1;

	// Check if already registered
	int32 Existing = FindSlot(EntityId);
	if (Existing != -1) return Existing;

	// Find empty slot
	for (int32 i = 0; i < MaxSlots; ++i)
	{
		if (EntityIds[i].load(std::memory_order_acquire) == 0)
		{
			HealthPacked[i].store(0, std::memory_order_relaxed);
			WeaponPacked[i].store(0, std::memory_order_relaxed);
			// Publish EntityId LAST (after data is zeroed) — readers use EntityId as publication flag
			EntityIds[i].store(EntityId, std::memory_order_release);
			return i;
		}
	}

	ensureMsgf(false, TEXT("FSimStateCache::Register: All %d slots full! Entity %lld not cached."),
		MaxSlots, EntityId);
	return -1;
}

void FSimStateCache::Unregister(int64 EntityId)
{
	int32 Slot = FindSlot(EntityId);
	if (Slot != -1)
	{
		// Clear data FIRST, then clear EntityId — EntityId is the publication flag
		HealthPacked[Slot].store(0, std::memory_order_relaxed);
		WeaponPacked[Slot].store(0, std::memory_order_relaxed);
		EntityIds[Slot].store(0, std::memory_order_release);
	}
}

// ═══════════════════════════════════════════════════════════════
// SIM THREAD: WRITE
// ═══════════════════════════════════════════════════════════════

void FSimStateCache::WriteHealth(int64 EntityId, float HP, float MaxHP)
{
	int32 Slot = FindSlot(EntityId);
	if (Slot != -1)
	{
		HealthPacked[Slot].store(SimStatePacking::PackHealth(HP, MaxHP), std::memory_order_release);
	}
}

void FSimStateCache::WriteWeapon(int64 EntityId, int32 Ammo, int32 MagSize, int32 Reserve, bool bReloading)
{
	int32 Slot = FindSlot(EntityId);
	if (Slot != -1)
	{
		WeaponPacked[Slot].store(SimStatePacking::PackWeapon(Ammo, MagSize, Reserve, bReloading), std::memory_order_release);
	}
}

// ═══════════════════════════════════════════════════════════════
// ANY THREAD: READ (double-check pattern prevents ABA on concurrent Unregister)
// ═══════════════════════════════════════════════════════════════

bool FSimStateCache::ReadHealth(int64 EntityId, FHealthSnapshot& Out) const
{
	int32 Slot = FindSlot(EntityId);
	if (Slot == -1) return false;

	Out = SimStatePacking::UnpackHealth(HealthPacked[Slot].load(std::memory_order_acquire));

	// Re-validate: slot still belongs to this entity (prevents ABA if Unregister+Register raced)
	if (EntityIds[Slot].load(std::memory_order_acquire) != EntityId) return false;
	return true;
}

bool FSimStateCache::ReadWeapon(int64 EntityId, FWeaponSnapshot& Out) const
{
	int32 Slot = FindSlot(EntityId);
	if (Slot == -1) return false;

	Out = SimStatePacking::UnpackWeapon(WeaponPacked[Slot].load(std::memory_order_acquire));

	// Re-validate: slot still belongs to this entity (prevents ABA if Unregister+Register raced)
	if (EntityIds[Slot].load(std::memory_order_acquire) != EntityId) return false;
	return true;
}

// ═══════════════════════════════════════════════════════════════
// SLOT LOOKUP (linear scan over contiguous EntityIds, 2 cache lines)
// ═══════════════════════════════════════════════════════════════

int32 FSimStateCache::FindSlot(int64 EntityId) const
{
	for (int32 i = 0; i < MaxSlots; ++i)
	{
		if (EntityIds[i].load(std::memory_order_acquire) == EntityId)
		{
			return i;
		}
	}
	return -1;
}
