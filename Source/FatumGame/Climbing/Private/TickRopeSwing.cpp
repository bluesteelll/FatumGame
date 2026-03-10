// Rope swing ability tick function — pendulum physics FSM.
// Sim-thread only. Character position controlled via constraint-projection.
// All positions/velocities in Jolt coordinates (Y=up, meters).

#include "AbilityTickFunctions.h"
#include "FlecsAbilityStates.h"
#include "FlecsMovementStatic.h"
#include "FlecsCharacterTypes.h"
#include "FWorldSimOwner.h"

// ═══════════════════════════════════════════════════════════════
// TICK ROPE SWING
// ═══════════════════════════════════════════════════════════════

EAbilityTickResult TickRopeSwing(FAbilityTickContext& Ctx, FAbilitySlot& Slot)
{
	FRopeSwingState* Swing = Ctx.Entity.try_get_mut<FRopeSwingState>();
	checkf(Swing, TEXT("TickRopeSwing: FRopeSwingState missing on entity"));

	const float DT = Ctx.DeltaTime;

	// Phase transition log (only on phase change)
	static uint8 LastLoggedPhase = 255;
	if (Swing->Phase != LastLoggedPhase)
	{
		UE_LOG(LogTemp, Log, TEXT("TickRopeSwing: Phase=%d RopeLen=%.2f Vel=[%.2f,%.2f,%.2f]"),
			Swing->Phase, Swing->CurrentRopeLength, Swing->VelX, Swing->VelY, Swing->VelZ);
		LastLoggedPhase = Swing->Phase;
	}

	switch (Swing->Phase)
	{
	case 0: // ── Enter: lerp from current position to rope attach point ──
	{
		Swing->PhaseTimer += DT;
		float Alpha = FMath::Clamp(Swing->PhaseTimer / FMath::Max(Swing->EnterLerpDuration, 0.001f), 0.f, 1.f);
		float EasedAlpha = Alpha * Alpha * (3.f - 2.f * Alpha);

		JPH::Vec3 Start(Swing->EnterStartX, Swing->EnterStartY, Swing->EnterStartZ);
		JPH::Vec3 Anchor(Swing->AnchorX, Swing->AnchorY, Swing->AnchorZ);
		JPH::Vec3 Target = Anchor - JPH::Vec3(0, Swing->CurrentRopeLength, 0);

		Ctx.FBChar->mCharacter->SetPosition(Start + (Target - Start) * EasedAlpha);
		Ctx.FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
		Ctx.FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());

		if (Swing->PhaseTimer >= Swing->EnterLerpDuration)
		{
			Swing->Phase = 1;
			Swing->PhaseTimer = 0.f;
			Swing->VelX = Swing->VelY = Swing->VelZ = 0.f;
		}
		return EAbilityTickResult::Continue;
	}

	case 1: // ── Swing/Climb: pendulum physics + rope climbing ──
	{
		JPH::Vec3 Anchor(Swing->AnchorX, Swing->AnchorY, Swing->AnchorZ);
		JPH::Vec3 CharPos = Ctx.FBChar->mCharacter->GetPosition();
		JPH::Vec3 Vel(Swing->VelX, Swing->VelY, Swing->VelZ);

		// Rope direction (anchor to character)
		JPH::Vec3 RopeDir = CharPos - Anchor;
		float RopeLen = RopeDir.Length();
		if (RopeLen < 0.001f) RopeLen = 0.001f;
		JPH::Vec3 RopeUnit = RopeDir / RopeLen;

		float InputLen = FMath::Sqrt(Ctx.DirX * Ctx.DirX + Ctx.DirZ * Ctx.DirZ);

		// ── Climb input: Sprint = up, Crouch = down ──
		float ClimbInput = 0.f;
		if (Ctx.bCrouchHeld)
			ClimbInput = -1.f;
		else if (Ctx.bSprinting)
			ClimbInput = 1.f;

		Swing->bClimbing = (FMath::Abs(ClimbInput) > 0.01f);

		// ── Apply climb (change rope length) ──
		if (Swing->bClimbing)
		{
			float Speed = (ClimbInput > 0.f) ? Swing->ClimbSpeedUp : Swing->ClimbSpeedDown;
			Swing->CurrentRopeLength -= Speed * ClimbInput * DT;
			Swing->CurrentRopeLength = FMath::Clamp(Swing->CurrentRopeLength,
				Swing->MinGrabLength, Swing->MaxRopeLength);

			// Top dismount: reached minimum rope length
			if (Swing->CurrentRopeLength <= Swing->MinGrabLength + 0.01f)
			{
				// Store dismount start position before transitioning
				Swing->EnterStartX = CharPos.GetX();
				Swing->EnterStartY = CharPos.GetY();
				Swing->EnterStartZ = CharPos.GetZ();
				Swing->Phase = 3;
				Swing->PhaseTimer = 0.f;
				return EAbilityTickResult::Continue;
			}
		}

		// ── Gravity (amplified for faster arcs) ──
		Vel = Vel + JPH::Vec3(0, -9.81f * Swing->SwingGravityMultiplier * DT, 0);

		// ── Swing input force (tangential to rope) ──
		if (InputLen > 0.1f && !Swing->bClimbing)
		{
			JPH::Vec3 InputForce(Ctx.DirX, 0, Ctx.DirZ);
			float IFLen = InputForce.Length();
			if (IFLen > 0.01f)
			{
				InputForce = InputForce / IFLen;
				// Project out radial component (keep only tangential)
				float RadialDot = InputForce.Dot(RopeUnit);
				InputForce = InputForce - RopeUnit * RadialDot;
				float TangLen = InputForce.Length();
				if (TangLen > 0.01f)
				{
					InputForce = InputForce * (Swing->SwingInputStrength * FMath::Min(IFLen, 1.f) / TangLen);
					Vel = Vel + InputForce * DT;
				}
			}
		}

		// ── Air drag ──
		float DragCoeff = Swing->AirDragCoefficient;
		if (Swing->bClimbing)
			DragCoeff *= Swing->ClimbDragMultiplier;
		Vel = Vel * FMath::Max(0.f, 1.f - DragCoeff * DT);

		// ── Integrate position ──
		JPH::Vec3 NewPos = CharPos + Vel * DT;

		// ── Constraint projection: enforce rope length ──
		JPH::Vec3 ToChar = NewPos - Anchor;
		float Dist = ToChar.Length();
		if (Dist > Swing->CurrentRopeLength && Dist > 0.001f)
		{
			JPH::Vec3 ToCharUnit = ToChar / Dist;
			NewPos = Anchor + ToCharUnit * Swing->CurrentRopeLength;

			// Remove outward radial velocity
			float RadialVel = Vel.Dot(ToCharUnit);
			if (RadialVel > 0.f)
				Vel = Vel - ToCharUnit * RadialVel;
		}

		// ── Jump off ──
		if (Ctx.bJumpPressed)
		{
			JPH::Vec3 JumpVel = Vel + JPH::Vec3(0, Swing->JumpOffBoost, 0);
			Ctx.FBChar->mCharacter->SetLinearVelocity(JumpVel);
			Ctx.FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
			Swing->Reset();
			return EAbilityTickResult::End;
		}

		// ── Let go: crouch edge while not climbing ──
		if (Ctx.bCrouchEdge && !Swing->bClimbing)
		{
			Ctx.FBChar->mCharacter->SetLinearVelocity(Vel);
			Ctx.FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
			Swing->Reset();
			return EAbilityTickResult::End;
		}

		// ── Apply position ──
		Ctx.FBChar->mCharacter->SetPosition(NewPos);
		Ctx.FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
		Ctx.FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());

		Swing->VelX = Vel.GetX();
		Swing->VelY = Vel.GetY();
		Swing->VelZ = Vel.GetZ();

		return EAbilityTickResult::Continue;
	}

	case 3: // ── TopDismount: lerp from current position to above anchor ──
	{
		Swing->PhaseTimer += DT;
		float Alpha = FMath::Clamp(Swing->PhaseTimer / FMath::Max(Swing->TopDismountDuration, 0.001f), 0.f, 1.f);
		float EasedAlpha = Alpha * Alpha * (3.f - 2.f * Alpha);

		JPH::Vec3 Start(Swing->EnterStartX, Swing->EnterStartY, Swing->EnterStartZ);
		JPH::Vec3 End(Swing->AnchorX, Swing->AnchorY + 0.2f, Swing->AnchorZ);

		Ctx.FBChar->mCharacter->SetPosition(Start + (End - Start) * EasedAlpha);
		Ctx.FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
		Ctx.FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());

		if (Swing->PhaseTimer >= Swing->TopDismountDuration)
		{
			Swing->Reset();
			return EAbilityTickResult::End;
		}
		return EAbilityTickResult::Continue;
	}

	default:
		checkf(false, TEXT("TickRopeSwing: Invalid phase %d"), Swing->Phase);
		Swing->Reset();
		return EAbilityTickResult::End;
	}
}
