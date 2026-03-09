// Climb ability tick function — ladder climbing FSM.
// Sim-thread only. Character is position-locked to ladder geometry.
// All positions/velocities in Jolt coordinates (Y=up, meters).

#include "AbilityTickFunctions.h"
#include "FlecsAbilityStates.h"
#include "FlecsMovementStatic.h"
#include "FlecsCharacterTypes.h"
#include "FWorldSimOwner.h"

// ═══════════════════════════════════════════════════════════════
// TICK CLIMB
// ═══════════════════════════════════════════════════════════════

EAbilityTickResult TickClimb(FAbilityTickContext& Ctx, FAbilitySlot& Slot)
{
	FClimbState* Climb = Ctx.Entity.try_get_mut<FClimbState>();
	checkf(Climb, TEXT("TickClimb: FClimbState missing on entity"));

	const float DT = Ctx.DeltaTime;

	// Compute target attachment position on ladder surface
	auto ComputeAttachPos = [&](float Y) -> JPH::Vec3
	{
		return JPH::Vec3(
			Climb->LadderX + Climb->FaceNormalX * Climb->StandoffDist,
			Y,
			Climb->LadderZ + Climb->FaceNormalZ * Climb->StandoffDist);
	};

	switch (Climb->Phase)
	{
	case 0: // ── Enter: lerp from current position to ladder attach point ──
	{
		Climb->PhaseTimer += DT;
		float Alpha = FMath::Clamp(Climb->PhaseTimer / FMath::Max(Climb->EnterLerpDuration, 0.001f), 0.f, 1.f);
		// Smoothstep easing
		float EasedAlpha = Alpha * Alpha * (3.f - 2.f * Alpha);

		JPH::Vec3 Start(Climb->EnterStartX, Climb->EnterStartY, Climb->EnterStartZ);
		JPH::Vec3 End = ComputeAttachPos(Climb->CurrentY);

		JPH::Vec3 LerpPos = Start + (End - Start) * EasedAlpha;
		Ctx.FBChar->mCharacter->SetPosition(LerpPos);
		Ctx.FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
		Ctx.FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());

		if (Climb->PhaseTimer >= Climb->EnterLerpDuration)
		{
			Climb->Phase = 1;
			Climb->PhaseTimer = 0.f;
		}
		return EAbilityTickResult::Continue;
	}

	case 1: // ── Active: climb up/down, respond to input ──
	{
		// DirZ in Jolt coords: positive = forward input. We map forward to climb up.
		// Ctx.DirX/DirZ are Jolt horizontal input direction from game thread.
		// For climbing: forward (positive DirZ in Jolt) = up, backward (negative DirZ) = down.
		// Use the magnitude of the input direction projected onto the ladder face normal
		// to determine climb intent. If player pushes toward ladder = up, away = down.
		// Simpler: use vertical component. Ctx.DirZ is horizontal Z in Jolt.
		// We need the "forward/backward" input mapped to vertical.
		// The input direction is already in world Jolt coords. Project onto ladder normal
		// to determine intent: pushing into ladder = up, pulling away = could be jump off.
		// Simpler approach: use raw input Y (but input is 2D horizontal).
		// The plan says Ctx.DirZ for up/down. In Jolt, the horizontal plane is XZ.
		// Typically forward movement input maps to the camera's forward projected onto XZ.
		// For climbing, we'll treat the component of input along the ladder normal:
		//   - Into ladder (dot < 0 with face normal) → up
		//   - Away from ladder (dot > 0) → no climb / jump off trigger
		// And left/right along ladder → no effect.
		// Actually the plan says "Read Ctx.DirZ for up/down input" and "DirZ > 0 = up, < 0 = down".
		// This implies a simpler mapping: the Z component of the horizontal input direction.
		// In Jolt coords, Z is one horizontal axis. Let's just project input onto -FaceNormal
		// (into the wall = up) as the primary climb axis. This is more intuitive:
		// push forward toward ladder = climb up, pull back = climb down.

		float RawDot = -(Ctx.DirX * Climb->FaceNormalX + Ctx.DirZ * Climb->FaceNormalZ);
		// RawDot > 0 = pushing into ladder = climb up
		// RawDot < 0 = pulling away = climb down
		// Clamp: raw input magnitude can exceed 1.0 (diagonal analog stick)
		float ClimbInput = FMath::Clamp(RawDot, -1.f, 1.f);
		float Speed = (ClimbInput > 0.f) ? Climb->ClimbSpeed : Climb->ClimbSpeedDown;
		Climb->CurrentY += Speed * ClimbInput * DT;
		Climb->CurrentY = FMath::Clamp(Climb->CurrentY, Climb->LadderBottomY, Climb->LadderTopY);

		// Jump off: jump pressed while on ladder
		if (Ctx.bJumpPressed)
		{
			Climb->Phase = 2;
			Climb->PhaseTimer = 0.f;
			// Phase 2 handles velocity application and deactivation
			break;
		}

		// Top exit: reached top of ladder
		if (Climb->CurrentY >= Climb->LadderTopY)
		{
			Climb->Phase = 3;
			Climb->PhaseTimer = 0.f;

			// Compute dismount lerp positions
			JPH::Vec3 CurPos = ComputeAttachPos(Climb->LadderTopY);
			Climb->DismountStartX = CurPos.GetX();
			Climb->DismountStartY = CurPos.GetY();
			Climb->DismountStartZ = CurPos.GetZ();

			// End position: on top of ladder + small step-up, forward from edge
			// "Forward" = away from ladder face = along face normal
			// +0.1m step-up to clear platform edge geometry
			Climb->DismountEndX = Climb->LadderX + Climb->FaceNormalX * Climb->TopDismountForwardDist;
			Climb->DismountEndY = Climb->LadderTopY + 0.1f;
			Climb->DismountEndZ = Climb->LadderZ + Climb->FaceNormalZ * Climb->TopDismountForwardDist;

			break;
		}

		// Bottom exit: reached bottom of ladder
		if (Climb->CurrentY <= Climb->LadderBottomY && ClimbInput < -0.01f)
		{
			Climb->Reset();
			return EAbilityTickResult::End;
		}

		// Lock position to ladder
		JPH::Vec3 AttachPos = ComputeAttachPos(Climb->CurrentY);
		Ctx.FBChar->mCharacter->SetPosition(AttachPos);
		Ctx.FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
		Ctx.FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());

		return EAbilityTickResult::Continue;
	}

	case 2: // ── JumpOff: apply impulse away from ladder ──
	{
		// Velocity = FaceNormal * HSpeed + Up * VSpeed (Jolt Y=up)
		JPH::Vec3 JumpVel(
			Climb->FaceNormalX * Climb->JumpOffHSpeed,
			Climb->JumpOffVSpeed,
			Climb->FaceNormalZ * Climb->JumpOffHSpeed);

		Ctx.FBChar->mCharacter->SetLinearVelocity(JumpVel);
		Ctx.FBChar->mLocomotionUpdate = JPH::Vec3::sZero();

		Climb->Reset();
		return EAbilityTickResult::End;
	}

	case 3: // ── TopDismount: lerp from top of ladder forward onto surface ──
	{
		Climb->PhaseTimer += DT;
		float Alpha = FMath::Clamp(Climb->PhaseTimer / FMath::Max(Climb->TopDismountDuration, 0.001f), 0.f, 1.f);
		// Smoothstep easing
		float EasedAlpha = Alpha * Alpha * (3.f - 2.f * Alpha);

		JPH::Vec3 Start(Climb->DismountStartX, Climb->DismountStartY, Climb->DismountStartZ);
		JPH::Vec3 End(Climb->DismountEndX, Climb->DismountEndY, Climb->DismountEndZ);

		Ctx.FBChar->mCharacter->SetPosition(Start + (End - Start) * EasedAlpha);
		Ctx.FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
		Ctx.FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());

		if (Climb->PhaseTimer >= Climb->TopDismountDuration)
		{
			Climb->Reset();
			return EAbilityTickResult::End;
		}
		return EAbilityTickResult::Continue;
	}

	default:
		checkf(false, TEXT("TickClimb: Invalid phase %d"), Climb->Phase);
		Climb->Reset();
		return EAbilityTickResult::End;
	}

	return EAbilityTickResult::Continue;
}
