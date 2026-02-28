// Mantle ability -- vault, mantle, and ledge grab traversal system.
// Activated on jump input when near a suitable ledge/wall.
// Physics (position lerp, hang pinning) runs on sim thread via FMantleInstance.
// This class is a thin game-thread wrapper for visuals, capsule, detection, and input routing.

#pragma once

#include "CoreMinimal.h"
#include "MovementAbility.h"
#include "MantleAbility.generated.h"

UENUM()
enum class EMantleType : uint8
{
	Vault,
	Mantle,
	LedgeGrab
};

/** Cached result of the 5-phase ledge detection algorithm. */
struct FLedgeCandidate
{
	FVector LedgeTopPoint = FVector::ZeroVector;   // UE world
	FVector WallHitPoint = FVector::ZeroVector;    // UE world
	FVector WallNormal = FVector::ForwardVector;   // UE world, unit
	float LedgeHeight = 0.f;                       // cm above character feet
	bool bCanPullUp = false;
	bool bValid = false;
};

UCLASS()
class FATUMGAME_API UMantleAbility : public UMovementAbility
{
	GENERATED_BODY()

public:
	// =====================================================================
	// LIFECYCLE
	// =====================================================================

	virtual bool CanActivate() const override;
	virtual void OnActivated() override;
	virtual void OnTick(float DeltaTime) override;
	virtual void OnDeactivated() override;

	// =====================================================================
	// OVERRIDES
	// =====================================================================

	virtual bool OwnsPosture() const override { return true; }
	virtual bool HandleJumpRequest() override;
	virtual ECharacterMoveMode GetMoveMode() const override;

	// =====================================================================
	// DETECTION (called externally from UFatumMovementComponent or from CanActivate)
	// =====================================================================

	/** Run 5-phase ledge detection using Barrage raycasts.
	 *  Results cached in CachedCandidate. */
	void PerformDetection(const FVector& CharFeetPos, const FVector& LookDir,
	                      float CapsuleRadius, float CapsuleHalfHeight);

	/** Clear cached detection result. Call on landing or state transitions. */
	void InvalidateCache() { CachedCandidate.bValid = false; }

	/** Read-only access to cached detection result. */
	const FLedgeCandidate& GetCachedCandidate() const { return CachedCandidate; }

	// =====================================================================
	// HANG EXIT COMMANDS (called from HandleJumpRequest / RequestCrouch)
	// =====================================================================

	/** Pull up onto the ledge (forward held + bCanPullUp). */
	void RequestPullUp();

	/** Wall jump in camera direction (no forward input during hang). */
	void RequestWallJump(const FVector& CameraForward);

	/** Let go and fall (crouch input during hang). */
	void RequestLetGo();

	// =====================================================================
	// SIM-THREAD SYNC
	// =====================================================================

	/** Called from ApplyBarrageSync with the latest hang state from sim thread. */
	void SetHangingFromSim(bool bHanging) { bInHangState = bHanging; }

	/** Is the character currently in the hanging sub-state? */
	bool IsHanging() const { return bInHangState; }

	/** Get the active mantle/vault/ledge-grab type. Only valid while active. */
	EMantleType GetActiveType() const { return ActiveType; }

	/** Get the cooldown timer (externally readable for detection suppression). */
	float GetCooldownRemaining() const { return LedgeGrabCooldownTimer; }

	/** Tick cooldown timer. Called from TickPostureAndEffects every frame, even when inactive. */
	void TickCooldown(float DeltaTime)
	{
		if (LedgeGrabCooldownTimer > 0.f)
		{
			LedgeGrabCooldownTimer -= DeltaTime;
			if (LedgeGrabCooldownTimer < 0.f) LedgeGrabCooldownTimer = 0.f;
		}
	}

private:
	FLedgeCandidate CachedCandidate;
	EMantleType ActiveType = EMantleType::Vault;

	// Active mantle ledge data — copied from CachedCandidate on activation.
	// Safe to read during hang/pull-up even after CachedCandidate is invalidated.
	FVector ActiveLedgeTopPoint = FVector::ZeroVector;
	FVector ActiveWallNormal = FVector::ForwardVector;
	bool bActiveCanPullUp = false;

	// Game-thread state
	bool bInHangState = false;
	float LedgeGrabCooldownTimer = 0.f;

	// Cached ledge-relative eye height for hang state
	float HangTargetEyeHeight = 60.f;
};
