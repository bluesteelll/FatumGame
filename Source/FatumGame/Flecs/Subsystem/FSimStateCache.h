// FSimStateCache — lock-free sim→game state bridge for scalar ECS values.
// Sim thread writes packed atomics, game thread reads. Zero locks, zero contention.
// SoA layout: 384 bytes total (6 cache lines, fits in L1).

#pragma once

#include "HAL/Platform.h"
#include <atomic>

// ─── Snapshot structs (for callers) ───

struct FHealthSnapshot
{
	float CurrentHP = 0.f;
	float MaxHP = 0.f;
};

struct FWeaponSnapshot
{
	int32 CurrentAmmo = 0;
	int32 MagazineSize = 0;
	int32 ReserveAmmo = 0;
	bool bIsReloading = false;
};

struct FResourceSnapshot
{
	float Ratios[4] = {0.f, 0.f, 0.f, 0.f};  // CurrentValue/MaxValue per pool (0.0-1.0)
	uint8 PoolCount = 0;
};

// ─── Packing utilities ───

namespace SimStatePacking
{
	// Health: upper 32 bits = HP float bits, lower 32 = MaxHP float bits
	inline uint64 PackHealth(float HP, float MaxHP)
	{
		uint32 HPBits, MaxBits;
		FMemory::Memcpy(&HPBits, &HP, sizeof(float));
		FMemory::Memcpy(&MaxBits, &MaxHP, sizeof(float));
		return (static_cast<uint64>(HPBits) << 32) | MaxBits;
	}

	inline FHealthSnapshot UnpackHealth(uint64 Packed)
	{
		FHealthSnapshot Snap;
		uint32 HPBits = static_cast<uint32>(Packed >> 32);
		uint32 MaxBits = static_cast<uint32>(Packed);
		FMemory::Memcpy(&Snap.CurrentHP, &HPBits, sizeof(float));
		FMemory::Memcpy(&Snap.MaxHP, &MaxBits, sizeof(float));
		return Snap;
	}

	// Weapon: [63]=bReloading, [47:32]=MagSize(16), [31:16]=Reserve(16), [15:0]=Ammo(16)
	inline uint64 PackWeapon(int32 Ammo, int32 MagSize, int32 Reserve, bool bReloading)
	{
		checkf(Ammo >= 0 && Ammo <= 65535, TEXT("PackWeapon: Ammo %d exceeds uint16 range"), Ammo);
		checkf(MagSize >= 0 && MagSize <= 65535, TEXT("PackWeapon: MagSize %d exceeds uint16 range"), MagSize);
		checkf(Reserve >= 0 && Reserve <= 65535, TEXT("PackWeapon: Reserve %d exceeds uint16 range"), Reserve);

		uint64 R = bReloading ? (1ULL << 63) : 0;
		uint64 M = static_cast<uint64>(static_cast<uint16>(MagSize)) << 32;
		uint64 Res = static_cast<uint64>(static_cast<uint16>(Reserve)) << 16;
		uint64 A = static_cast<uint64>(static_cast<uint16>(Ammo));
		return R | M | Res | A;
	}

	inline FWeaponSnapshot UnpackWeapon(uint64 Packed)
	{
		FWeaponSnapshot Snap;
		Snap.bIsReloading = (Packed >> 63) != 0;
		Snap.MagazineSize = static_cast<int32>(static_cast<uint16>(Packed >> 32));
		Snap.ReserveAmmo = static_cast<int32>(static_cast<uint16>(Packed >> 16));
		Snap.CurrentAmmo = static_cast<int32>(static_cast<uint16>(Packed));
		return Snap;
	}

	// Resource: 4 pools x 8-bit ratio (0-255 → 0.0-1.0) + 8 bits pool count
	// Layout: [39:32]=PoolCount [31:24]=Pool3 [23:16]=Pool2 [15:8]=Pool1 [7:0]=Pool0
	inline uint64 PackResources(const float Ratios[4], uint8 PoolCount)
	{
		uint64 R0 = static_cast<uint64>(static_cast<uint8>(FMath::Clamp(Ratios[0], 0.f, 1.f) * 255.f));
		uint64 R1 = static_cast<uint64>(static_cast<uint8>(FMath::Clamp(Ratios[1], 0.f, 1.f) * 255.f)) << 8;
		uint64 R2 = static_cast<uint64>(static_cast<uint8>(FMath::Clamp(Ratios[2], 0.f, 1.f) * 255.f)) << 16;
		uint64 R3 = static_cast<uint64>(static_cast<uint8>(FMath::Clamp(Ratios[3], 0.f, 1.f) * 255.f)) << 24;
		uint64 Count = static_cast<uint64>(PoolCount) << 32;
		return Count | R3 | R2 | R1 | R0;
	}

	inline FResourceSnapshot UnpackResources(uint64 Packed)
	{
		FResourceSnapshot Snap;
		Snap.Ratios[0] = static_cast<float>(static_cast<uint8>(Packed)) / 255.f;
		Snap.Ratios[1] = static_cast<float>(static_cast<uint8>(Packed >> 8)) / 255.f;
		Snap.Ratios[2] = static_cast<float>(static_cast<uint8>(Packed >> 16)) / 255.f;
		Snap.Ratios[3] = static_cast<float>(static_cast<uint8>(Packed >> 24)) / 255.f;
		Snap.PoolCount = static_cast<uint8>(Packed >> 32);
		return Snap;
	}
}

// ─── The cache (SoA layout) ───

class FSimStateCache
{
public:
	static constexpr int32 MaxSlots = 16;

	FSimStateCache();

	// ─── Sim thread: registration ───

	/** Register an entity for cache tracking. Returns slot index or -1 if full. */
	int32 Register(int64 EntityId);

	/** Unregister an entity. Clears its slot. */
	void Unregister(int64 EntityId);

	// ─── Sim thread: write (after each ECS mutation) ───

	void WriteHealth(int64 EntityId, float HP, float MaxHP);
	void WriteWeapon(int64 EntityId, int32 Ammo, int32 MagSize, int32 Reserve, bool bReloading);
	void WriteResources(int64 EntityId, const float Ratios[4], uint8 PoolCount);

	// ─── Game thread: read (safe from any thread) ───

	bool ReadHealth(int64 EntityId, FHealthSnapshot& Out) const;
	bool ReadWeapon(int64 EntityId, FWeaponSnapshot& Out) const;
	bool ReadResources(int64 EntityId, FResourceSnapshot& Out) const;

private:
	// SoA: IDs contiguous for fast FindSlot scan (128 bytes = 2 cache lines)
	alignas(64) std::atomic<int64>  EntityIds[MaxSlots];      // 128 bytes
	alignas(64) std::atomic<uint64> HealthPacked[MaxSlots];   // 128 bytes
	alignas(64) std::atomic<uint64> WeaponPacked[MaxSlots];   // 128 bytes
	alignas(64) std::atomic<uint64> ResourcePacked[MaxSlots]; // 128 bytes
	// Total: 512 bytes (8 cache lines)

	/** Linear scan over EntityIds array. Returns slot index or -1. */
	int32 FindSlot(int64 EntityId) const;
};
