// UFlecsWeaponProfile implementation — constructor defaults and state multiplier lookup.

#include "FlecsWeaponProfile.h"
#include "FlecsMovementComponents.h"

UFlecsWeaponProfile::UFlecsWeaponProfile()
{
	// Walk: slight base spread penalty
	WalkMultipliers.SpreadBaseMultiplier = 1.5f;
	WalkMultipliers.BloomMultiplier = 1.2f;

	// Sprint: significant penalties across the board
	SprintMultipliers.SpreadBaseMultiplier = 3.0f;
	SprintMultipliers.BloomMultiplier = 2.0f;
	SprintMultipliers.SwayMultiplier = 1.5f;
	SprintMultipliers.KickMultiplier = 1.3f;
	SprintMultipliers.BobMultiplier = 1.5f;

	// Airborne: worst accuracy
	AirborneMultipliers.SpreadBaseMultiplier = 4.0f;
	AirborneMultipliers.BloomMultiplier = 2.5f;
	AirborneMultipliers.SwayMultiplier = 2.0f;
	AirborneMultipliers.KickMultiplier = 1.5f;

	// Crouch: improved stability
	CrouchMultipliers.SpreadBaseMultiplier = 0.6f;
	CrouchMultipliers.BloomMultiplier = 0.7f;
	CrouchMultipliers.SwayMultiplier = 0.6f;
	CrouchMultipliers.KickMultiplier = 0.8f;
	CrouchMultipliers.BobMultiplier = 0.5f;

	// Slide: poor accuracy, no bob
	SlideMultipliers.SpreadBaseMultiplier = 3.0f;
	SlideMultipliers.BloomMultiplier = 2.0f;
	SlideMultipliers.KickMultiplier = 1.5f;
	SlideMultipliers.BobMultiplier = 0.f;
}

const FWeaponStateMultipliers& UFlecsWeaponProfile::GetStateMultipliers(EWeaponMoveState State) const
{
	switch (State)
	{
	case EWeaponMoveState::Walk:     return WalkMultipliers;
	case EWeaponMoveState::Sprint:   return SprintMultipliers;
	case EWeaponMoveState::Airborne: return AirborneMultipliers;
	case EWeaponMoveState::Crouch:   return CrouchMultipliers;
	case EWeaponMoveState::Slide:    return SlideMultipliers;
	default:                         return IdleMultipliers;
	}
}

EWeaponMoveState ResolveWeaponMoveState(uint8 MoveMode, uint8 Posture)
{
	const auto Mode = static_cast<ECharacterMoveMode>(MoveMode);

	switch (Mode)
	{
	case ECharacterMoveMode::Slide:     return EWeaponMoveState::Slide;
	case ECharacterMoveMode::Jump:
	case ECharacterMoveMode::Fall:
	case ECharacterMoveMode::Mantle:
	case ECharacterMoveMode::Vault:
	case ECharacterMoveMode::LedgeHang:
	case ECharacterMoveMode::Blink:     return EWeaponMoveState::Airborne;
	case ECharacterMoveMode::Sprint:    return EWeaponMoveState::Sprint;
	default: break; // Idle, Walk, Lean — fall through to posture check
	}

	if (static_cast<ECharacterPosture>(Posture) == ECharacterPosture::Crouching
		|| static_cast<ECharacterPosture>(Posture) == ECharacterPosture::Prone)
		return EWeaponMoveState::Crouch;

	if (Mode == ECharacterMoveMode::Walk)
		return EWeaponMoveState::Walk;

	return EWeaponMoveState::Idle;
}
