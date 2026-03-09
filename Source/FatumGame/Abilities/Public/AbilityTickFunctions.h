// Ability tick function dispatch table + signatures.
// Each ability type registers a tick function indexed by EAbilityTypeId.
// Called by AbilityLifecycleManager on sim thread.

#pragma once
#include "flecs.h"
#include "FlecsAbilityTypes.h"
#include "SkeletonTypes.h"

class FBCharacterBase;
struct FMovementStatic;
struct FCharacterPhysBridge;
struct FCharacterInputAtomics;
struct FCharacterSimState;
class UBarrageDispatch;

// Context passed to all ability tick functions
struct FAbilityTickContext
{
	flecs::entity Entity;
	FBCharacterBase* FBChar;
	const FMovementStatic* MovementStatic;
	FCharacterPhysBridge* Bridge;
	float DeltaTime;
	float VelocityScale;
	float DirX, DirZ;           // input direction (Jolt coords)
	bool bJumpPressed;
	bool bCrouchHeld;
	bool bSprinting;
	bool bCrouchEdge;
	bool bOnGround;
	// Extended context for Blink/Mantle
	const FCharacterInputAtomics* Input;
	UBarrageDispatch* Barrage;
	FSkeletonKey CharacterKey;
	FCharacterSimState* SimState;
	// Extended context for KineticBlast (needs CharacterBridges for cone impulse)
	TArray<FCharacterPhysBridge>* CharacterBridges;
};

// Tick function signature
using FAbilityTickFn = EAbilityTickResult (*)(FAbilityTickContext& Ctx, FAbilitySlot& Slot);

// Global dispatch table (indexed by EAbilityTypeId)
extern FAbilityTickFn GAbilityTickFunctions[static_cast<int32>(EAbilityTypeId::MAX)];

void InitAbilityTickFunctions();

// Shared helper: cancel slide ability if active (used by blink teleport)
void CancelSlideAbility(flecs::entity Entity);

// Individual ability tick functions
EAbilityTickResult TickSlide(FAbilityTickContext& Ctx, FAbilitySlot& Slot);
EAbilityTickResult TickBlink(FAbilityTickContext& Ctx, FAbilitySlot& Slot);
EAbilityTickResult TickMantle(FAbilityTickContext& Ctx, FAbilitySlot& Slot);
EAbilityTickResult TickKineticBlast(FAbilityTickContext& Ctx, FAbilitySlot& Slot);
EAbilityTickResult TickTelekinesis(FAbilityTickContext& Ctx, FAbilitySlot& Slot);
EAbilityTickResult TickClimb(FAbilityTickContext& Ctx, FAbilitySlot& Slot);

// Telekinesis release helper (also called by lifecycle manager on forced deactivation)
struct FTelekinesisState;
struct FTelekinesisConfig;
void ReleaseTelekinesisObject(FTelekinesisState& State, FAbilityTickContext& Ctx,
	bool bThrow, const FTelekinesisConfig* Config, FAbilitySlot* Slot = nullptr);
