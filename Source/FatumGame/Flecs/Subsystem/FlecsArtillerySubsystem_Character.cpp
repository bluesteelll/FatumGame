// FlecsArtillerySubsystem - Character Physics Bridge
// RegisterCharacterBridge, PrepareCharacterStep (mantle/slide FSMs),
// ComputeFreshAlpha, RegisterLocalPlayer.
//
// All character locomotion runs on the sim thread (before StackUp).
// Position readback is in AFlecsCharacter::Tick (game thread, direct Jolt read).

#include "FlecsArtillerySubsystem.h"
#include "FlecsCharacter.h"
#include "FlecsCharacterTypes.h"
#include "FlecsMovementStatic.h"
#include "FBarragePrimitive.h"
#include "BarrageDispatch.h"
#include "FWorldSimOwner.h"
#include "HAL/PlatformTime.h"

// ═══════════════════════════════════════════════════════════════
// CHARACTER PHYSICS BRIDGE
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::RegisterCharacterBridge(AFlecsCharacter* Character)
{
	check(Character);
	check(Character->CachedBarrageBody.IsValid());
	check(Character->InputAtomics.IsValid());

	FCharacterPhysBridge Bridge;
	Bridge.CachedBody = Character->CachedBarrageBody;
	Bridge.InputAtomics = Character->InputAtomics;
	Bridge.CharacterKey = Character->CharacterKey;
	Bridge.SlideActive = Character->SlideActiveAtomic;
	Bridge.MantleActive = Character->MantleActiveAtomic;
	Bridge.Hanging = Character->HangingAtomic;

	// Resolve Flecs entity for this character (bidirectional binding already set)
	Bridge.Entity = GetEntityForBarrageKey(Character->CharacterKey);

	// Cache FBCharacterBase pointer for direct sim-thread access (no per-tick lookup)
	if (CachedBarrageDispatch && CachedBarrageDispatch->JoltGameSim
		&& CachedBarrageDispatch->JoltGameSim->CharacterToJoltMapping)
	{
		TSharedPtr<FBCharacterBase>* CharPtr =
			CachedBarrageDispatch->JoltGameSim->CharacterToJoltMapping->Find(
				Character->CachedBarrageBody->KeyIntoBarrage);
		if (CharPtr && *CharPtr)
		{
			Bridge.CachedFBChar = *CharPtr;
		}
	}
	checkf(Bridge.CachedFBChar, TEXT("RegisterCharacterBridge: Failed to resolve FBCharacter for key %llu"),
		static_cast<uint64>(Character->CharacterKey));

	CharacterBridges.Add(MoveTemp(Bridge));
}

void UFlecsArtillerySubsystem::UnregisterCharacterBridge(FSkeletonKey CharacterKey)
{
	CharacterBridges.RemoveAll([CharacterKey](const FCharacterPhysBridge& B) { return B.CharacterKey == CharacterKey; });
}

// ═══════════════════════════════════════════════════════════════
// FRESH ALPHA COMPUTATION (for AFlecsCharacter::Tick, runs before subsystem Tick)
// ═══════════════════════════════════════════════════════════════

float UFlecsArtillerySubsystem::ComputeFreshAlpha(uint64& OutSimTick) const
{
	OutSimTick = SimWorker.SimTickCount.load(std::memory_order_acquire);
	const float SimDt = SimWorker.LastSimDeltaTime.load(std::memory_order_acquire);
	const double LastSimTime = SimWorker.LastSimTickTimeSeconds.load(std::memory_order_acquire);

	if (SimDt > 0.0f && LastSimTime > 0.0)
	{
		const double TimeSince = FPlatformTime::Seconds() - LastSimTime;
		if (TimeSince >= 0.0)
		{
			return FMath::Clamp(static_cast<float>(TimeSince / SimDt), 0.0f, 1.0f);
		}
	}
	return 1.0f;
}

// ═══════════════════════════════════════════════════════════════
// MANTLE FSM (sim thread) — called by PrepareCharacterStep
// ═══════════════════════════════════════════════════════════════

/** Easing function for mantle phases. */
static float MantleEaseAlpha(uint8 Phase, float LinearAlpha)
{
	if (Phase == 1) // Rise: EaseOut quadratic
		return 1.f - (1.f - LinearAlpha) * (1.f - LinearAlpha);
	if (Phase == 0 || Phase == 2) // GrabTransition or Pull: Smoothstep
		return LinearAlpha * LinearAlpha * (3.f - 2.f * LinearAlpha);
	return LinearAlpha; // Land: linear
}

/** Run mantle FSM for one character. Sets bOutMantling/bOutHanging.
 *  May remove FMantleInstance from Entity. */
static void StepMantleInstance(flecs::entity Entity, FBCharacterBase* FBChar,
                                FMantleInstance* Mantle, const FMovementStatic* MS,
                                float DeltaTime, bool& bOutMantling, bool& bOutHanging)
{
	bOutMantling = true;
	bOutHanging = false;
	Mantle->Timer += DeltaTime;

	if (Mantle->Phase == 4) // Hanging
	{
		bOutHanging = true;
		JPH::Vec3 HangPos(Mantle->EndX, Mantle->EndY, Mantle->EndZ);
		FBChar->mCharacter->SetPosition(HangPos);
		FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
		FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());

		// Check timeout (LedgeGrabMaxDuration=0 means infinite)
		float MaxDur = MS->LedgeGrabMaxDuration;
		if (MaxDur > 0.f && Mantle->Timer > MaxDur)
		{
			Entity.remove<FMantleInstance>();
			bOutMantling = false;
			bOutHanging = false;
		}
		return;
	}

	// GrabTransition, Rise, Pull, Land (Phase 0-3)
	float Alpha = FMath::Clamp(Mantle->Timer / FMath::Max(Mantle->PhaseDuration, 0.001f), 0.f, 1.f);
	float EasedAlpha = MantleEaseAlpha(Mantle->Phase, Alpha);

	JPH::Vec3 Start(Mantle->StartX, Mantle->StartY, Mantle->StartZ);
	JPH::Vec3 End(Mantle->EndX, Mantle->EndY, Mantle->EndZ);
	FBChar->mCharacter->SetPosition(Start + (End - Start) * EasedAlpha);
	FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
	FBChar->mCharacter->SetLinearVelocity(JPH::Vec3::sZero());

	// Phase completion
	if (Mantle->Timer < Mantle->PhaseDuration) return;

	Mantle->Timer = 0.f;

	// GrabTransition → Hanging (skip Rise/Pull/Land for initial LedgeGrab)
	if (Mantle->Phase == 0 && Mantle->MantleType == 2)
	{
		Mantle->Phase = 4;
		bOutHanging = true;
		return;
	}

	Mantle->Phase++;

	if (Mantle->Phase == 2) // Rise→Pull
	{
		Mantle->StartX = Mantle->EndX; Mantle->StartY = Mantle->EndY; Mantle->StartZ = Mantle->EndZ;
		Mantle->EndX = Mantle->PullEndX; Mantle->EndY = Mantle->PullEndY; Mantle->EndZ = Mantle->PullEndZ;
		Mantle->PhaseDuration = Mantle->PullDuration;
	}
	else if (Mantle->Phase == 3) // Pull→Land
	{
		Mantle->StartX = Mantle->PullEndX; Mantle->StartY = Mantle->PullEndY; Mantle->StartZ = Mantle->PullEndZ;
		Mantle->EndX   = Mantle->PullEndX; Mantle->EndY   = Mantle->PullEndY; Mantle->EndZ   = Mantle->PullEndZ;
		Mantle->PhaseDuration = Mantle->LandDuration;
	}
	else if (Mantle->Phase == 4) // Land→complete or Hanging
	{
		if (Mantle->MantleType == 2) // LedgeGrab after pull-up → re-hang
		{
			bOutHanging = true;
			Mantle->Timer = 0.f;
		}
		else // Vault/Mantle complete
		{
			Entity.remove<FMantleInstance>();
			bOutMantling = false;
		}
	}
	else if (Mantle->Phase > 4)
	{
		Entity.remove<FMantleInstance>();
		bOutMantling = false;
	}
}

// ═══════════════════════════════════════════════════════════════
// SLIDE STEP (sim thread) — called by PrepareCharacterStep
// ═══════════════════════════════════════════════════════════════

/** Run slide deceleration + steering for one character. Returns true if still sliding.
 *  May remove FSlideInstance from Entity.
 *  If returns true, mLocomotionUpdate is already set (caller should skip normal locomotion). */
static bool StepSlideInstance(flecs::entity Entity, FBCharacterBase* FBChar,
                               FSlideInstance* Slide, const FMovementStatic* MS,
                               float DirX, float DirZ, float DeltaTime)
{
	// Capture slide direction on first tick (from current Jolt velocity)
	if (Slide->SlideDirX == 0.f && Slide->SlideDirZ == 0.f)
	{
		JPH::Vec3 CurVel = FBChar->mCharacter->GetLinearVelocity();
		float HorizLen = FMath::Sqrt(CurVel.GetX() * CurVel.GetX() + CurVel.GetZ() * CurVel.GetZ());
		if (HorizLen > 0.01f)
		{
			Slide->SlideDirX = CurVel.GetX() / HorizLen;
			Slide->SlideDirZ = CurVel.GetZ() / HorizLen;
		}
	}

	Slide->CurrentSpeed -= MS->SlideDeceleration * DeltaTime;
	Slide->CurrentSpeed = FMath::Max(Slide->CurrentSpeed, 0.f);
	Slide->Timer -= DeltaTime;

	// Exit conditions
	bool bOnGround = (FBChar->mCharacter->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);
	if (Slide->Timer <= 0.f || Slide->CurrentSpeed < MS->SlideMinExitSpeed || !bOnGround)
	{
		Entity.remove<FSlideInstance>();
		return false;
	}

	// Direct velocity control (bypasses MoveTowards — slide owns deceleration)
	float SpeedJolt = Slide->CurrentSpeed / 100.f;
	JPH::Vec3 SlideVel(Slide->SlideDirX * SpeedJolt, 0, Slide->SlideDirZ * SpeedJolt);

	// Minor steering from input
	float SlideDirInputLen = FMath::Sqrt(DirX * DirX + DirZ * DirZ);
	if (SlideDirInputLen > 0.01f)
	{
		float SteerRate = MS->SlideMinAcceleration / 100.f;
		float InvDirLen = 1.f / SlideDirInputLen;
		JPH::Vec3 SteerTarget(DirX * InvDirLen * SpeedJolt, 0, DirZ * InvDirLen * SpeedJolt);
		JPH::Vec3 SteerDiff = SteerTarget - SlideVel;
		float SteerLen = SteerDiff.Length();
		float SteerStep = SteerRate * DeltaTime;
		if (SteerLen > SteerStep && SteerLen > 1.0e-6f)
		{
			SlideVel = SlideVel + (SteerDiff / SteerLen) * SteerStep;
		}
	}

	FBChar->mLocomotionUpdate = SlideVel;
	return true;
}

// ═══════════════════════════════════════════════════════════════
// PREPARE CHARACTER STEP (sim thread, before StackUp)
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::PrepareCharacterStep(float DeltaTime)
{
	for (FCharacterPhysBridge& Bridge : CharacterBridges)
	{
		if (!Bridge.CachedFBChar || !Bridge.InputAtomics) continue;
		FBCharacterBase* FBChar = Bridge.CachedFBChar.Get();

		// 1. Read input direction from atomics (latest-wins, lock-free)
		float DirX = Bridge.InputAtomics->DirX.load(std::memory_order_relaxed);
		float DirZ = Bridge.InputAtomics->DirZ.load(std::memory_order_relaxed);

		// 2. Read movement params from Flecs
		const FMovementStatic* MS = Bridge.Entity.is_valid() ? Bridge.Entity.try_get<FMovementStatic>() : nullptr;
		const FCharacterMoveState* State = Bridge.Entity.is_valid() ? Bridge.Entity.try_get<FCharacterMoveState>() : nullptr;
		if (!MS)
		{
			FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
			continue;
		}

		// 2.5. Mantle/Vault/LedgeGrab position control
		FMantleInstance* Mantle = Bridge.Entity.try_get_mut<FMantleInstance>();
		bool bMantling = false;
		bool bHanging = false;
		if (Mantle)
		{
			StepMantleInstance(Bridge.Entity, FBChar, Mantle, MS, DeltaTime, bMantling, bHanging);
		}
		Bridge.MantleActive->store(bMantling, std::memory_order_relaxed);
		Bridge.Hanging->store(bHanging, std::memory_order_relaxed);

		if (bMantling)
		{
			Bridge.SlideActive->store(false, std::memory_order_relaxed);
			continue;
		}

		// 3. Slide deceleration
		FSlideInstance* Slide = Bridge.Entity.try_get_mut<FSlideInstance>();
		bool bSliding = Slide ? StepSlideInstance(Bridge.Entity, FBChar, Slide, MS, DirX, DirZ, DeltaTime) : false;
		Bridge.SlideActive->store(bSliding, std::memory_order_relaxed);

		if (bSliding) continue;

		// 5. Compute target speed from posture/sprint/slide
		float TargetSpeedCm;
		float AccelCm;
		if (State && State->bSprinting && State->Posture == 0)
		{
			TargetSpeedCm = MS->SprintSpeed;
			AccelCm = MS->SprintAcceleration;
		}
		else
		{
			TargetSpeedCm = MS->WalkSpeed;
			AccelCm = MS->GroundAcceleration;
			if (State)
			{
				switch (State->Posture)
				{
				case 1: TargetSpeedCm = MS->CrouchSpeed; break;
				case 2: TargetSpeedCm = MS->ProneSpeed; break;
				}
			}
		}

		// 6. Ground state from Jolt CharacterVirtual
		bool bOnGround = (FBChar->mCharacter->GetGroundState()
			== JPH::CharacterVirtual::EGroundState::OnGround);

		// 7. Deceleration + air accel (cm/s^2)
		float DecelCm = MS->GroundDeceleration;
		float AirAccelCm = MS->AirAcceleration;

		// 8. Build target horizontal velocity (UE cm/s → Jolt m/s)
		float DirLen = FMath::Sqrt(DirX * DirX + DirZ * DirZ);
		JPH::Vec3 TargetH = JPH::Vec3::sZero();
		if (DirLen > 0.01f)
		{
			float InvDirLen = 1.f / DirLen;
			float SpeedJolt = TargetSpeedCm / 100.f;  // cm→m
			TargetH = JPH::Vec3(DirX * InvDirLen * SpeedJolt, 0, DirZ * InvDirLen * SpeedJolt);
		}

		// 9. Read current horizontal from CharacterVirtual
		JPH::Vec3 CurVelo = FBChar->mCharacter->GetLinearVelocity();
		JPH::Vec3 CurH(CurVelo.GetX(), 0, CurVelo.GetZ());

		// 10. Pick accel rate (m/s^2)
		float AccelRate;
		if (bOnGround)
			AccelRate = TargetH.IsNearZero() ? (DecelCm / 100.f) : (AccelCm / 100.f);
		else
			AccelRate = AirAccelCm / 100.f;

		// 11. MoveTowards: smooth current → target
		JPH::Vec3 Diff = TargetH - CurH;
		float DiffLen = Diff.Length();
		float Step = AccelRate * DeltaTime;
		JPH::Vec3 SmoothedH;
		if (DiffLen <= Step || DiffLen < 1.0e-6f)
			SmoothedH = TargetH;
		else
			SmoothedH = CurH + (Diff / DiffLen) * Step;

		// 12. Write pre-smoothed velocity to FBCharacter (consumed by StepCharacter)
		FBChar->mLocomotionUpdate = SmoothedH;
	}
}

// ═══════════════════════════════════════════════════════════════
// LOCAL PLAYER REGISTRATION
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::RegisterLocalPlayer(AActor* Player, FSkeletonKey Key)
{
	CachedLocalPlayerActor = Player;
	CachedLocalPlayerKey = Key;
}

void UFlecsArtillerySubsystem::UnregisterLocalPlayer()
{
	CachedLocalPlayerActor = nullptr;
	CachedLocalPlayerKey = FSkeletonKey();
}
