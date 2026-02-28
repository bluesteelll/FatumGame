// Movement state enums and Flecs component for character movement tracking.

#pragma once

#include "CoreMinimal.h"
#include "FlecsMovementComponents.generated.h"

UENUM(BlueprintType)
enum class ECharacterPosture : uint8
{
	Standing = 0,
	Crouching = 1,
	Prone = 2
};

UENUM(BlueprintType)
enum class ECharacterMoveMode : uint8
{
	Idle = 0,
	Walk = 1,
	Sprint = 2,
	Jump = 3,
	Fall = 4,
	Slide = 5,
	Mantle = 6,
	Vault = 7,
	Lean = 8,
	LedgeHang = 9
};

UENUM(BlueprintType)
enum class ELeanDirection : uint8
{
	None = 0,
	Left = 1,
	Right = 2
};

/**
 * Movement state component for character Flecs entities.
 * Written from game thread via EnqueueCommand on state change only.
 * Read by ECS systems that need movement context (damage modifiers, AI, etc.)
 * NOT a USTRUCT -- pure Flecs component (no Blueprint exposure from ECS side).
 */
struct FMovementState
{
	uint8 Posture = 0;         // ECharacterPosture
	uint8 MoveMode = 0;       // ECharacterMoveMode
	float Speed = 0.f;        // Current horizontal speed cm/s
	float VerticalSpeed = 0.f; // Current vertical speed cm/s
	uint8 LeanDir = 0;        // ELeanDirection
};
