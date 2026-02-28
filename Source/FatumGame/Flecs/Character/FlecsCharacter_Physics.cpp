// Physics bridge implementation for AFlecsCharacter.
// Barrage position readback, interpolation, CMC feed, posture→Jolt shape, movement ECS sync.

#include "FlecsCharacter.h"
#include "FatumMovementComponent.h"
#include "FlecsMovementComponents.h"
#include "FlecsMovementStatic.h"
#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "EPhysicsLayer.h"
#include "FlecsMovementProfile.h"
#include "MantleAbility.h"
#include "Components/CapsuleComponent.h"

// ═══════════════════════════════════════════════════════════════════════════
// BARRAGE POSITION READ (game thread, in Tick — BEFORE CameraManager at TG_PostPhysics)
// Direct Jolt read → Prev/Curr double-buffer → Alpha lerp → VInterpTo → ApplyBarrageSync.
// Same pattern as ISM UpdateTransforms + VInterpTo smoothing (camera amplifies tick-boundary jitter).
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::ReadAndApplyBarragePosition(float DeltaTime)
{
	if (!FBarragePrimitive::IsNotNull(CachedBarrageBody)) return;

	UFlecsArtillerySubsystem* Sub = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!Sub) return;

	// 1. Fresh Alpha from SimWorker atomics (not cached — character ticks before subsystem).
	uint64 SimTick;
	float Alpha = Sub->ComputeFreshAlpha(SimTick);

	// 2. Read position directly from Jolt
	FVector3f Pos3f = FBarragePrimitive::GetPosition(CachedBarrageBody);
	FVector PhysPos(Pos3f);
	if (PhysPos.ContainsNaN()) return;

	// 3. Read ground state + velocity
	uint8 GS = static_cast<uint8>(FBarragePrimitive::GetCharacterGroundState(CachedBarrageBody));
	FVector3f Vel3f = FBarragePrimitive::GetVelocity(CachedBarrageBody);
	FVector Vel(Vel3f);

	// 4. Prev/Curr double-buffer (same as ISM FEntityTransformState)
	if (SimTick > LastBarrageSimTick)
	{
		PrevBarragePos = CurrBarragePos;
		CurrBarragePos = PhysPos;
		LastBarrageSimTick = SimTick;
	}

	// 5. Interpolate + smooth
	FVector FeetPos;
	if (bBarrageJustSpawned)
	{
		PrevBarragePos = PhysPos;
		CurrBarragePos = PhysPos;
		SmoothedBarragePos = PhysPos;
		FeetPos = PhysPos;
		bBarrageJustSpawned = false;
	}
	else
	{
		FVector LerpTarget = FMath::Lerp(PrevBarragePos, CurrBarragePos, Alpha);
		SmoothedBarragePos = FMath::VInterpTo(SmoothedBarragePos, LerpTarget, DeltaTime, 30.f);
		FeetPos = SmoothedBarragePos;
	}

	// 6. Slide state from sim thread
	bool bSlideActive = SlideActiveAtomic ? SlideActiveAtomic->load(std::memory_order_relaxed) : false;

	// 7. Apply (CMC feed + FeetToActorOffset + SetActorLocation)
	ApplyBarrageSync(FeetPos, GS, Vel, bSlideActive);
}

// ═══════════════════════════════════════════════════════════════════════════
// BARRAGE POSITION APPLY (feeds CMC, offsets feet→actor, teleports actor)
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::ApplyBarrageSync(const FVector& FeetPos, uint8 GS, const FVector& Vel, bool bSlideActive)
{
	// Feed CMC state. Velocity + MovementMode set here (not in CMC::TickComponent)
	// so TickPostureAndEffects has fresh data when called from actor Tick.
	if (FatumMovement)
	{
		FatumMovement->SetBarrageGroundState(GS);
		FatumMovement->SetBarrageVelocity(Vel);
		FatumMovement->Velocity = Vel;
		FatumMovement->SetMovementMode(GS == 0 ? MOVE_Walking : MOVE_Falling);
	}

	// On ground: snap FeetToActorOffset to capsule HH (eye height handles smooth transitions).
	// In air: offset is FROZEN so posture change pulls legs up, not head down.
	const bool bGrounded = FatumMovement && FatumMovement->IsMovingOnGround();
	if (bGrounded)
	{
		FeetToActorOffset = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	// TeleportPhysics: skip UE capsule overlap tests — Barrage is physics authority.
	FVector FinalPos = FeetPos + FVector(0, 0, FeetToActorOffset);
	SetActorLocation(FinalPos, false, nullptr, ETeleportType::TeleportPhysics);

	// Sim-thread slide exit detection
	if (FatumMovement)
	{
		uint64 SimTick = LastBarrageSimTick;

		if (FatumMovement->IsSliding())
		{
			if (SlideActivationSimTick == 0)
			{
				SlideActivationSimTick = SimTick;
			}

			if (!bSlideActive && SimTick > SlideActivationSimTick + 3)
			{
				FatumMovement->DeactivateAbility();
				SlideActivationSimTick = 0;
			}
		}
		else
		{
			SlideActivationSimTick = 0;
		}

		// Mantle exit detection (same 3-tick grace pattern as slide)
		bool bMantleActive = MantleActiveAtomic ? MantleActiveAtomic->load(std::memory_order_relaxed) : false;
		bool bHanging = HangingAtomic ? HangingAtomic->load(std::memory_order_relaxed) : false;

		if (FatumMovement->IsMantling())
		{
			if (MantleActivationSimTick == 0)
			{
				MantleActivationSimTick = SimTick;
			}

			if (!bMantleActive && SimTick > MantleActivationSimTick + 3)
			{
				FatumMovement->DeactivateAbility();
				MantleActivationSimTick = 0;
			}
		}
		else
		{
			MantleActivationSimTick = 0;
		}

		// Sync hang state to MantleAbility (for HandleJumpRequest routing)
		if (UMantleAbility* MA = FatumMovement->FindAbility<UMantleAbility>())
		{
			MA->SetHangingFromSim(bHanging);
		}
	}

	// Jump impulse: consume from CMC → send as OtherForce to Barrage
	if (FatumMovement && FBarragePrimitive::IsNotNull(CachedBarrageBody))
	{
		float JumpImpulse = FatumMovement->ConsumePendingJumpImpulse();
		if (JumpImpulse > 0.f)
		{
			FBarragePrimitive::ApplyForce(
				FVector3d(0, 0, JumpImpulse), CachedBarrageBody,
				PhysicsInputType::OtherForce);
		}
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// POSTURE → BARRAGE SHAPE (capsule resize on posture change)
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::HandlePostureChanged(ECharacterPosture NewPosture)
{
	check(FatumMovement && FatumMovement->MovementProfile);

	float R, HH;
	FatumMovement->MovementProfile->GetCapsuleForPosture(NewPosture, R, HH);

	// Convert UE cm to Jolt meters.
	// UE HH = total half-extent (cylinder half + hemisphere radius).
	// Jolt CapsuleShape wants CYLINDER half-height only = HH - R.
	// Clamp to minimum 0.01m for near-sphere capsules (e.g. prone HH < R).
	double JoltHH = FMath::Max((HH - R) / 100.0, 0.01);
	double JoltR = R / 100.0;

	FSkeletonKey Key = CharacterKey;
	UFlecsArtillerySubsystem* Sub = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (Sub && Key.IsValid())
	{
		Sub->EnqueueCommand([Key, JoltHH, JoltR]()
		{
			UBarrageDispatch* P = UBarrageDispatch::SelfPtr;
			if (!P) return;

			// CharacterVirtual: update both outer shape and inner body shape
			P->SetCharacterCapsuleShape(Key, JoltHH, JoltR);
		});
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// MOVEMENT STATE → ECS SYNC (posture/mode change detection)
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::SyncMovementStateToECS()
{
	if (!FatumMovement) return;

	uint8 CurPosture = static_cast<uint8>(FatumMovement->GetCurrentPosture());
	uint8 CurMode = static_cast<uint8>(FatumMovement->GetCurrentMoveMode());

	// Only write to Flecs when state actually changes
	if (CurPosture == LastSyncedPosture && CurMode == LastSyncedMoveMode) return;

	LastSyncedPosture = CurPosture;
	LastSyncedMoveMode = CurMode;

	bool bSprinting = FatumMovement->IsSprinting();
	float Speed = FatumMovement->GetBarrageVelocity().Size2D();
	float VSpeed = FatumMovement->GetBarrageVelocity().Z;
	FSkeletonKey Key = CharacterKey;

	UFlecsArtillerySubsystem* FlecsSubsystem = GetWorld()->GetSubsystem<UFlecsArtillerySubsystem>();
	if (!FlecsSubsystem || !Key.IsValid()) return;

	FlecsSubsystem->EnqueueCommand(
		[FlecsSubsystem, Key, CurPosture, CurMode, Speed, VSpeed, bSprinting]()
	{
		flecs::entity Entity = FlecsSubsystem->GetEntityForBarrageKey(Key);
		if (!Entity.is_valid()) return;

		// AnimBP cosmetic state (FMovementState)
		FMovementState State;
		State.Posture = CurPosture;
		State.MoveMode = CurMode;
		State.Speed = Speed;
		State.VerticalSpeed = VSpeed;
		Entity.set<FMovementState>(State);

		// Sim thread authority state (FCharacterMoveState) — read by PrepareCharacterStep
		FCharacterMoveState MoveState;
		MoveState.bSprinting = bSprinting;
		MoveState.Posture = CurPosture;
		Entity.set<FCharacterMoveState>(MoveState);
	});
}
