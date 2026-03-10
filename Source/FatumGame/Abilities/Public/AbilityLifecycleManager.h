// Ability lifecycle: tick active abilities + recharge inactive slots.
// Priority model: exclusive > always-on > active > recharge.
// Activation remains in PrepareCharacterStep to preserve priority order.

#pragma once
#include "flecs.h"
#include "SkeletonTypes.h"

struct FCharacterPhysBridge;
class FBCharacterBase;
struct FMovementStatic;
struct FCharacterInputAtomics;
struct FCharacterSimState;
class UBarrageDispatch;

struct FAbilityTickResults
{
	bool bSlideActive = false;         // slide is currently active
	bool bAnyMovementAbility = false;  // any movement ability active (skip locomotion)
	bool bJumpConsumed = false;        // jump consumed by ability (skip normal jump)
	bool bBlinkAiming = false;         // blink in aim mode (game thread: time dilation)
	bool bBlinkTeleported = false;     // blink teleported this frame
	bool bMantling = false;            // mantle FSM active
	bool bHanging = false;             // mantle in hang phase
	uint8 MantleType = 0;             // 0=Vault, 1=Mantle, 2=LedgeGrab
	bool bTelekinesisActive = false;   // telekinesis holding an object
	bool bClimbing = false;            // climbing a ladder
	bool bRopeSwinging = false;        // swinging on a rope
};

FAbilityTickResults TickAbilities(
	flecs::entity Entity,
	FBCharacterBase* FBChar,
	const FMovementStatic* MS,
	FCharacterPhysBridge& Bridge,
	float DeltaTime,
	float VelocityScale,
	float DirX, float DirZ,
	bool bJumpPressed,
	bool bCrouchHeld,
	bool bSprinting,
	bool bCrouchEdge,
	bool bOnGround,
	const FCharacterInputAtomics* Input,
	UBarrageDispatch* Barrage,
	FSkeletonKey CharacterKey,
	FCharacterSimState* SimState,
	TArray<FCharacterPhysBridge>* CharacterBridges);
