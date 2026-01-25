// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Entity type classification derived from FSkeletonKey

#pragma once

#include "skeletonize.h"
#include "EEntityType.generated.h"

// Entity types derived from skeleton key infixes
// These map directly to SFIX values for O(1) lookup
UENUM(BlueprintType)
enum class EEntityType : uint8
{
	Unknown = 0,        // SFIX_NONE or unrecognized
	Component = 1,      // SFIX_ACT_COMP (0xF)
	GunArchetype = 2,   // SFIX_ART_GUNS (0x1)
	GunInstance = 3,    // SFIX_ART_1GUN (0x2)
	Projectile = 4,     // SFIX_GUN_SHOT (0x3) - bullets, missiles, etc.
	BarragePrimitive = 5, // SFIX_BAR_PRIM (0x4)
	Actor = 6,          // SFIX_ART_ACTS (0x5) - generic actors (monsters, furniture, etc.)
	Fcm = 7,            // SFIX_ART_FCMS (0x6)
	Fact = 8,           // SFIX_ART_FACT (0x7)
	Player = 9,         // SFIX_PLAYERID (0x8)
	Bone = 10,          // SFIX_BONEKEY (0x9)
	MassEntity = 11,    // SFIX_MASSIDP (0xA)
	Constellation = 12, // SFIX_STELLAR (0xB)
	Lord = 13,          // SFIX_SK_LORD (0xF)

	NUM_TYPES
};

namespace EntityTypeUtils
{
	// Lookup table: SFIX nibble (0-15) -> EEntityType
	// Index is the top nibble value
	inline constexpr EEntityType SfixToEntityType[16] = {
		EEntityType::Unknown,         // 0x0 - SFIX_NONE
		EEntityType::GunArchetype,    // 0x1 - SFIX_ART_GUNS
		EEntityType::GunInstance,     // 0x2 - SFIX_ART_1GUN
		EEntityType::Projectile,      // 0x3 - SFIX_GUN_SHOT
		EEntityType::BarragePrimitive,// 0x4 - SFIX_BAR_PRIM
		EEntityType::Actor,           // 0x5 - SFIX_ART_ACTS
		EEntityType::Fcm,             // 0x6 - SFIX_ART_FCMS
		EEntityType::Fact,            // 0x7 - SFIX_ART_FACT
		EEntityType::Player,          // 0x8 - SFIX_PLAYERID
		EEntityType::Bone,            // 0x9 - SFIX_BONEKEY
		EEntityType::MassEntity,      // 0xA - SFIX_MASSIDP
		EEntityType::Constellation,   // 0xB - SFIX_STELLAR
		EEntityType::Unknown,         // 0xC - unused
		EEntityType::Unknown,         // 0xD - unused
		EEntityType::Unknown,         // 0xE - unused
		EEntityType::Component,       // 0xF - SFIX_ACT_COMP / SFIX_SK_LORD
	};

	// Extract entity type from skeleton key - O(1)
	FORCEINLINE EEntityType GetEntityType(uint64 Key)
	{
		// Top nibble is at bits 60-63
		uint8 Nibble = static_cast<uint8>((Key >> 60) & 0xF);
		return SfixToEntityType[Nibble];
	}

	// Check if key is of specific type
	FORCEINLINE bool IsOfType(uint64 Key, EEntityType Type)
	{
		return GetEntityType(Key) == Type;
	}

	// Quick checks for common types
	FORCEINLINE bool IsProjectile(uint64 Key) { return IsOfType(Key, EEntityType::Projectile); }
	FORCEINLINE bool IsActor(uint64 Key) { return IsOfType(Key, EEntityType::Actor); }
	FORCEINLINE bool IsPlayer(uint64 Key) { return IsOfType(Key, EEntityType::Player); }
	FORCEINLINE bool IsGunInstance(uint64 Key) { return IsOfType(Key, EEntityType::GunInstance); }
}
