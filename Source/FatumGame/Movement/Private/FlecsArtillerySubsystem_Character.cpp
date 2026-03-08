// FlecsArtillerySubsystem - Character Physics Bridge
// RegisterCharacterBridge, PrepareCharacterStep,
// ComputeFreshAlpha, RegisterLocalPlayer.
//
// All character locomotion runs on the sim thread (before StackUp).
// Position readback is in AFlecsCharacter::Tick (game thread, direct Jolt read).
// Ability FSMs (Slide/Blink/Mantle) are in AbilityTickFunctions.cpp,
// called via TickAbilities in AbilityLifecycleManager.

#include "FlecsArtillerySubsystem.h"
#include "FlecsCharacter.h"
#include "FlecsCharacterTypes.h"
#include "FlecsMovementStatic.h"
#include "FBarragePrimitive.h"
#include "BarrageDispatch.h"
#include "FWorldSimOwner.h"
#include "CoordinateUtils.h"
#include "LedgeDetector.h"
#include "PhysicsFilters/FastObjectLayerFilters.h"
#include "EPhysicsLayer.h"
#include "HAL/PlatformTime.h"
#include "AbilityLifecycleManager.h"
#include "AbilityTickFunctions.h"
#include "FlecsAbilityTypes.h"
#include "FlecsAbilityStates.h"
#include "FlecsResourceTypes.h"

// ═══════════════════════════════════════════════════════════════
// CHARACTER PHYSICS BRIDGE
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::RegisterCharacterBridge(AFlecsCharacter* Character)
{
	check(Character);
	check(Character->CachedBarrageBody.IsValid());
	check(Character->InputAtomics.IsValid());
	check(Character->StateAtomics.IsValid());

	FCharacterPhysBridge Bridge;
	Bridge.CachedBody = Character->CachedBarrageBody;
	Bridge.InputAtomics = Character->InputAtomics;
	Bridge.CharacterKey = Character->CharacterKey;
	Bridge.StateAtomics = Character->StateAtomics;

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
// MANTLE ACTIVATION HELPERS (sim thread)
// ═══════════════════════════════════════════════════════════════

/** Geometry: estimated distance from hands (gripping ledge) to feet. */
static float ComputeHandToFeetDist(float StandingHalfHeight)
{
	return StandingHalfHeight * 2.f - 25.f;
}

/** Try to activate mantle on sim thread. Runs FLedgeDetector, populates FMantleState.
 *  Returns true if mantle was activated (caller should skip normal jump). */
static bool TryActivateMantle(flecs::entity Entity, FBCharacterBase* FBChar,
                               const FMovementStatic* MS, FCharacterSimState* SimState,
                               const FCharacterInputAtomics* Input,
                               UBarrageDispatch* Barrage, FSkeletonKey CharKey,
                               bool bOnGround)
{
	if (SimState->MantleCooldownTimer > 0.f) return false;

	// Check if mantle slot exists and is inactive
	FAbilitySystem* AbilSys = Entity.try_get_mut<FAbilitySystem>();
	if (!AbilSys) return false;
	int32 MantleIdx = AbilSys->FindSlotByType(EAbilityTypeId::Mantle);
	if (MantleIdx == INDEX_NONE || AbilSys->IsSlotActive(MantleIdx)) return false;

	// Resource cost check (before expensive ledge detection raycasts)
	FAbilitySlot& MantleSlot = AbilSys->Slots[MantleIdx];
	if (MantleSlot.HasActivationCosts())
	{
		FResourcePools* Pools = Entity.try_get_mut<FResourcePools>();
		if (!Pools || !CheckActivationCosts(*Pools, MantleSlot)) return false;
	}

	// Get character feet position from Jolt
	JPH::Vec3 JoltPos = FBChar->mCharacter->GetPosition();
	FVector3d FeetPosD = CoordinateUtils::FromJoltCoordinatesD(JoltPos);
	FVector FeetPos(FeetPosD);

	// Camera look direction for detection
	FVector LookDir(
		Input->CamDirX.Read(),
		Input->CamDirY.Read(),
		Input->CamDirZ.Read());
	if (LookDir.IsNearlyZero()) return false;
	LookDir.Normalize();

	float MaxReach = bOnGround ? MS->MantleMaxHeight : MS->LedgeGrabMaxHeight;

	FLedgeCandidate Candidate;
	FLedgeDetector::Detect(FeetPos, LookDir,
	                       MS->StandingRadius, MS->StandingHalfHeight,
	                       MaxReach, MS, Barrage, CharKey, Candidate);

	if (!Candidate.bValid) return false;

	// Determine type
	uint8 MType;
	if (!bOnGround)
		MType = 2; // LedgeGrab
	else if (Candidate.LedgeHeight <= MS->MantleVaultMaxHeight)
		MType = 0; // Vault
	else
		MType = 1; // Mantle

	// Compute geometry (UE coords → Jolt coords)
	const FVector& LedgeTop = Candidate.LedgeTopPoint;
	const FVector& WallNormal = Candidate.WallNormal;
	float CapsuleR = MS->CrouchRadius; // capsule will be crouch during mantle

	FVector PullEndPos = FVector(
		LedgeTop.X + WallNormal.X * (CapsuleR + 10.f),
		LedgeTop.Y + WallNormal.Y * (CapsuleR + 10.f),
		LedgeTop.Z);

	JPH::Vec3 JoltStart = CoordinateUtils::ToJoltCoordinates(FVector3d(FeetPos));
	JPH::Vec3 JoltPullEnd = CoordinateUtils::ToJoltCoordinates(FVector3d(PullEndPos));
	JPH::Vec3 JoltWallN = CoordinateUtils::ToJoltUnitVector(FVector3d(WallNormal));

	// Populate FMantleState (permanent component)
	FMantleState* Mantle = Entity.try_get_mut<FMantleState>();
	checkf(Mantle, TEXT("TryActivateMantle: FMantleState missing on entity"));

	Mantle->StartX = JoltStart.GetX(); Mantle->StartY = JoltStart.GetY(); Mantle->StartZ = JoltStart.GetZ();
	Mantle->PullEndX = JoltPullEnd.GetX(); Mantle->PullEndY = JoltPullEnd.GetY(); Mantle->PullEndZ = JoltPullEnd.GetZ();
	Mantle->WallNormalX = JoltWallN.GetX(); Mantle->WallNormalZ = JoltWallN.GetZ();
	Mantle->PullDuration = MS->MantlePullDuration;
	Mantle->LandDuration = MS->MantleLandDuration;
	Mantle->Timer = 0.f;
	Mantle->MantleType = MType;
	Mantle->bCanPullUp = Candidate.bCanPullUp;

	if (MType == 2) // LedgeGrab
	{
		float HandToFeet = ComputeHandToFeetDist(MS->StandingHalfHeight);
		FVector HangFeetPos = FVector(
			LedgeTop.X + WallNormal.X * (CapsuleR + 5.f),
			LedgeTop.Y + WallNormal.Y * (CapsuleR + 5.f),
			LedgeTop.Z - HandToFeet);
		JPH::Vec3 JoltHang = CoordinateUtils::ToJoltCoordinates(FVector3d(HangFeetPos));
		Mantle->EndX = JoltHang.GetX(); Mantle->EndY = JoltHang.GetY(); Mantle->EndZ = JoltHang.GetZ();
		Mantle->PhaseDuration = MS->LedgeGrabTransitionDuration;
		Mantle->Phase = 0; // GrabTransition
	}
	else // Vault / Mantle
	{
		FVector RiseEndPos = FeetPos;
		RiseEndPos.Z = LedgeTop.Z;
		JPH::Vec3 JoltRiseEnd = CoordinateUtils::ToJoltCoordinates(FVector3d(RiseEndPos));
		Mantle->EndX = JoltRiseEnd.GetX(); Mantle->EndY = JoltRiseEnd.GetY(); Mantle->EndZ = JoltRiseEnd.GetZ();
		Mantle->PhaseDuration = MS->MantleRiseDuration;
		Mantle->Phase = 1; // Rise (skip GrabTransition)
	}

	// Commit resource costs (checked earlier before ledge detection)
	if (MantleSlot.HasActivationCosts())
	{
		FResourcePools* Pools = Entity.try_get_mut<FResourcePools>();
		if (Pools) CommitActivationCosts(*Pools, MantleSlot);
	}

	AbilSys->ActivateSlot(MantleIdx);
	return true;
}

// ═══════════════════════════════════════════════════════════════
// PREPARE CHARACTER STEP (sim thread, before StackUp)
// ═══════════════════════════════════════════════════════════════

void UFlecsArtillerySubsystem::PrepareCharacterStep(float RealDT, float DilatedDT, float TimeScale, bool bPlayerFullSpeed)
{
	// Player compensation: when bPlayerFullSpeed, velocities/forces must be scaled
	// so that Jolt integration with DilatedDT produces RealDT-equivalent displacement.
	const float VelocityScale = (bPlayerFullSpeed && TimeScale > 0.02f) ? (1.f / TimeScale) : 1.f;
	const float DeltaTime = bPlayerFullSpeed ? RealDT : DilatedDT;

	for (FCharacterPhysBridge& Bridge : CharacterBridges)
	{
		if (!Bridge.CachedFBChar || !Bridge.InputAtomics) continue;
		FBCharacterBase* FBChar = Bridge.CachedFBChar.Get();
		const FCharacterInputAtomics* Input = Bridge.InputAtomics.Get();

		// Lazy-capture base gravity (first tick after character registers)
		if (!Bridge.bBaseGravityCaptured && FBChar->mGravity.GetY() != 0.f)
		{
			Bridge.BaseGravityJoltY = FBChar->mGravity.GetY();
			Bridge.bBaseGravityCaptured = true;
		}

		// Compensate player gravity during time dilation (preserve X/Z for gravity zones)
		if (Bridge.bBaseGravityCaptured)
		{
			float TargetGravY = Bridge.BaseGravityJoltY * VelocityScale;
			FBChar->mGravity = JPH::Vec3(FBChar->mGravity.GetX(), TargetGravY, FBChar->mGravity.GetZ());
		}

		// ── 1. Read ALL input atomics ──
		float DirX = Input->DirX.Read();
		float DirZ = Input->DirZ.Read();
		bool bJumpPressed = Input->JumpPressed.Consume();
		bool bCrouchHeld = Input->CrouchHeld.Read();
		bool bSprinting = Input->Sprinting.Read();
		bool bAbility2Pressed = Input->Ability2Pressed.Consume();

		// ── 2. Read Flecs components ──
		if (!Bridge.Entity.is_valid())
		{
			FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
			continue;
		}

		const FMovementStatic* MS = Bridge.Entity.try_get<FMovementStatic>();
		const FCharacterMoveState* State = Bridge.Entity.try_get<FCharacterMoveState>();
		FCharacterSimState* SimState = Bridge.Entity.try_get_mut<FCharacterSimState>();
		if (!MS)
		{
			FBChar->mLocomotionUpdate = JPH::Vec3::sZero();
			continue;
		}

		// Edge-detect crouch (using SimState prev values)
		// Jump is already one-shot (Consume() returns true once) — no edge detect needed
		bool bCrouchEdge = false;
		if (SimState)
		{
			bCrouchEdge = bCrouchHeld && !SimState->bPrevCrouchHeld;
			SimState->bPrevCrouchHeld = bCrouchHeld;
		}

		// Ground state
		bool bOnGround = (FBChar->mCharacter->GetGroundState()
			== JPH::CharacterVirtual::EGroundState::OnGround);

		// ── 2.5. Kinetic Blast activation (instant one-shot) ──
		FResourcePools* Pools = Bridge.Entity.try_get_mut<FResourcePools>();
		if (bAbility2Pressed)
		{
			FAbilitySystem* AbilSys = Bridge.Entity.try_get_mut<FAbilitySystem>();
			if (AbilSys)
			{
				int32 KBIdx = AbilSys->FindSlotByType(EAbilityTypeId::KineticBlast);
				if (KBIdx != INDEX_NONE && !AbilSys->IsSlotActive(KBIdx))
				{
					FAbilitySlot& KBSlot = AbilSys->Slots[KBIdx];
					if (KBSlot.CooldownTimer <= 0.f && KBSlot.Charges != 0)
					{
						// Resource cost check (all-or-nothing)
						bool bAffordable = !KBSlot.HasActivationCosts() || (Pools && CheckActivationCosts(*Pools, KBSlot));
						if (bAffordable)
						{
							if (KBSlot.Charges > 0) KBSlot.Charges--;
							if (Pools && KBSlot.HasActivationCosts()) CommitActivationCosts(*Pools, KBSlot);
							AbilSys->ActivateSlot(KBIdx);
						}
					}
				}
			}
		}

		// ── 3. Ability System: tick all abilities (mantle, blink, slide, KB) ──
		FAbilityTickResults AbilityResults = TickAbilities(
			Bridge.Entity, FBChar, MS, Bridge,
			DeltaTime, VelocityScale,
			DirX, DirZ, bJumpPressed, bCrouchHeld, bSprinting, bCrouchEdge, bOnGround,
			Input, CachedBarrageDispatch, Bridge.CharacterKey, SimState,
			&CharacterBridges);

		// ── 3.5. Write atomics from results ──
		Bridge.StateAtomics->MantleActive.Write(AbilityResults.bMantling);
		Bridge.StateAtomics->Hanging.Write(AbilityResults.bHanging);
		if (AbilityResults.bMantling)
		{
			Bridge.StateAtomics->MantleType.Write(AbilityResults.MantleType);
		}

		bool bSliding = AbilityResults.bSlideActive;
		Bridge.StateAtomics->SlideActive.Write(bSliding);
		Bridge.StateAtomics->BlinkAiming.Write(AbilityResults.bBlinkAiming);
		Bridge.StateAtomics->TelekinesisActive.Write(AbilityResults.bTelekinesisActive);

		if (AbilityResults.bBlinkTeleported)
		{
			bSliding = false;
			Bridge.StateAtomics->SlideActive.Write(false);
		}

		if (AbilityResults.bJumpConsumed)
			bJumpPressed = false;

		// ── 4. Skip to timers if any movement ability is active ──
		if (AbilityResults.bMantling || bSliding || AbilityResults.bBlinkTeleported)
			goto TickTimers;

		{
			// ── 5. No movement ability active — check activations ──

			// 5a. Jump → try mantle detection → if found → activate mantle slot
			if (bJumpPressed)
			{
				bool bMantleCreated = false;
				if (SimState && CachedBarrageDispatch)
				{
					bMantleCreated = TryActivateMantle(Bridge.Entity, FBChar, MS, SimState,
					                                    Input, CachedBarrageDispatch,
					                                    Bridge.CharacterKey, bOnGround);
				}

				if (bMantleCreated)
				{
					Bridge.StateAtomics->MantleActive.Write(true);
					const FMantleState* NewMantle = Bridge.Entity.try_get<FMantleState>();
					if (NewMantle) Bridge.StateAtomics->MantleType.Write(NewMantle->MantleType);
					goto TickTimers;
				}

				// 5b. No mantle → normal jump (with coyote time)
				if (SimState && (bOnGround || SimState->CoyoteTimer > 0.f))
				{
					float JumpVel = (State && State->Posture == 1) ? MS->CrouchJumpVelocity : MS->JumpVelocity;
					FBarragePrimitive::ApplyForce(
						FVector3d(0, 0, JumpVel * VelocityScale),
						Bridge.CachedBody, PhysicsInputType::OtherForce);
					SimState->CoyoteTimer = 0.f;
					SimState->JumpBufferTimer = 0.f;
				}
				else if (SimState)
				{
					// Airborne, no coyote → buffer jump
					SimState->JumpBufferTimer = MS->JumpBufferSeconds;
				}
			}

			// 5c. Crouch press + sprint + ground + speed → slide activation (via AbilitySystem)
			{
				FAbilitySystem* AbilSys = Bridge.Entity.try_get_mut<FAbilitySystem>();
				if (AbilSys && bCrouchEdge && bSprinting && bOnGround)
				{
					int32 SlideIdx = AbilSys->FindSlotByType(EAbilityTypeId::Slide);
					if (SlideIdx != INDEX_NONE && !AbilSys->IsSlotActive(SlideIdx))
					{
						JPH::Vec3 CurVel = FBChar->mCharacter->GetLinearVelocity();
						float SpeedScale = (VelocityScale > 1.001f) ? (1.f / VelocityScale) : 1.f;
						float HorizSpeedCm = FMath::Sqrt(CurVel.GetX() * CurVel.GetX() + CurVel.GetZ() * CurVel.GetZ()) * 100.f * SpeedScale;
						if (HorizSpeedCm >= MS->SlideMinEntrySpeed)
						{
							FAbilitySlot& SlideSlot = AbilSys->Slots[SlideIdx];
							bool bAffordable = !SlideSlot.HasActivationCosts() || (Pools && CheckActivationCosts(*Pools, SlideSlot));
							if (bAffordable)
							{
								if (Pools && SlideSlot.HasActivationCosts()) CommitActivationCosts(*Pools, SlideSlot);
								FSlideState* Slide = Bridge.Entity.try_get_mut<FSlideState>();
								checkf(Slide, TEXT("FSlideState missing"));
								Slide->CurrentSpeed = HorizSpeedCm + MS->SlideInitialSpeedBoost;
								Slide->Timer = MS->SlideMaxDuration;
								Slide->SlideDirX = 0.f;
								Slide->SlideDirZ = 0.f;
								AbilSys->ActivateSlot(SlideIdx);
								Bridge.StateAtomics->SlideActive.Write(true);
								goto TickTimers;
							}
						}
					}
				}
			}

			// ── 6. Normal locomotion ──
			float TargetSpeedCm;
			float AccelCm;
			if (bSprinting && (!State || State->Posture == 0))
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

			float DecelCm = MS->GroundDeceleration;
			float AirAccelCm = MS->AirAcceleration;

			float DirLen = FMath::Sqrt(DirX * DirX + DirZ * DirZ);
			JPH::Vec3 TargetH = JPH::Vec3::sZero();
			if (DirLen > 0.01f)
			{
				float InvDirLen = 1.f / DirLen;
				float SpeedJolt = TargetSpeedCm / 100.f;
				TargetH = JPH::Vec3(DirX * InvDirLen * SpeedJolt, 0, DirZ * InvDirLen * SpeedJolt);
			}

			JPH::Vec3 CurVelo = FBChar->mCharacter->GetLinearVelocity();
			JPH::Vec3 CurH(CurVelo.GetX(), 0, CurVelo.GetZ());

			// Jolt velocity includes VelocityScale from previous frame's mLocomotionUpdate.
			// Undo scaling to work in logical (unscaled) velocity space for smoothing.
			// Without this, VelocityScale compounds each frame → runaway acceleration.
			if (VelocityScale > 1.001f)
			{
				CurH *= (1.f / VelocityScale);
			}

			float AccelRate;
			if (bOnGround)
				AccelRate = TargetH.IsNearZero() ? (DecelCm / 100.f) : (AccelCm / 100.f);
			else
				AccelRate = AirAccelCm / 100.f;

			JPH::Vec3 Diff = TargetH - CurH;
			float DiffLen = Diff.Length();
			float MoveStep = AccelRate * DeltaTime;
			JPH::Vec3 SmoothedH;
			if (DiffLen <= MoveStep || DiffLen < 1.0e-6f)
				SmoothedH = TargetH;
			else
				SmoothedH = CurH + (Diff / DiffLen) * MoveStep;

			FBChar->mLocomotionUpdate = SmoothedH * VelocityScale;
		}

	TickTimers:
		// ── 6.5. Write resource snapshot to SimStateCache ──
		if (Pools && Pools->PoolCount > 0)
		{
			float Ratios[4] = {};
			for (int32 p = 0; p < Pools->PoolCount; ++p)
				Ratios[p] = Pools->Pools[p].GetRatio();
			SimStateCache.WriteResources(static_cast<int64>(Bridge.Entity.id()), Ratios, Pools->PoolCount);
		}

		// ── 7. Tick sim-thread timers ──
		if (SimState)
		{
			// Coyote time: start when leaving ground (not from jump)
			if (SimState->bWasGrounded && !bOnGround)
			{
				SimState->CoyoteTimer = MS->CoyoteTimeSeconds;
			}
			if (SimState->CoyoteTimer > 0.f) SimState->CoyoteTimer -= DeltaTime;

			// Jump buffer: consume on landing (must check BEFORE updating bWasGrounded)
			if (!SimState->bWasGrounded && bOnGround && SimState->JumpBufferTimer > 0.f)
			{
				// Buffered jump on landing
				float JumpVel = (State && State->Posture == 1) ? MS->CrouchJumpVelocity : MS->JumpVelocity;
				FBarragePrimitive::ApplyForce(
					FVector3d(0, 0, JumpVel * VelocityScale),
					Bridge.CachedBody, PhysicsInputType::OtherForce);
				SimState->JumpBufferTimer = 0.f;
			}
			if (SimState->JumpBufferTimer > 0.f) SimState->JumpBufferTimer -= DeltaTime;

			SimState->bWasGrounded = bOnGround;

			// Mantle cooldown
			if (SimState->MantleCooldownTimer > 0.f)
			{
				SimState->MantleCooldownTimer -= DeltaTime;
				if (SimState->MantleCooldownTimer < 0.f) SimState->MantleCooldownTimer = 0.f;
			}

			// 10Hz airborne ledge detection (for auto-ledge-grab)
			FAbilitySystem* AbilSys = Bridge.Entity.try_get_mut<FAbilitySystem>();
			bool bMantleActive = AbilSys && AbilSys->IsAbilityActive(EAbilityTypeId::Mantle);
			if (!bOnGround && !bMantleActive && CachedBarrageDispatch
				&& SimState->MantleCooldownTimer <= 0.f)
			{
				SimState->AirDetectionTimer -= DeltaTime;
				if (SimState->AirDetectionTimer <= 0.f)
				{
					SimState->AirDetectionTimer = 0.1f; // 10Hz

					JPH::Vec3 JoltPos = FBChar->mCharacter->GetPosition();
					FVector3d FeetPosD = CoordinateUtils::FromJoltCoordinatesD(JoltPos);
					FVector FeetPos(FeetPosD);
					FVector LookDir(
						Input->CamDirX.Read(),
						Input->CamDirY.Read(),
						Input->CamDirZ.Read());
					if (!LookDir.IsNearlyZero())
					{
						LookDir.Normalize();
						FLedgeCandidate Candidate;
						FLedgeDetector::Detect(FeetPos, LookDir,
						                       MS->StandingRadius, MS->StandingHalfHeight,
						                       MS->LedgeGrabMaxHeight, MS, CachedBarrageDispatch,
						                       Bridge.CharacterKey, Candidate);

						// Auto-grab if valid and player is pressing jump
						// (or if falling past a ledge — could be auto-detect)
						// For now: just cache. The jump input will use it next frame.
						// Actually: if we detect a ledge while airborne, auto-activate
						// (Dishonored auto-grabs ledges when falling near them)
						// Let's keep it manual: only on jump press
					}
				}
			}
			else
			{
				SimState->AirDetectionTimer = 0.f;
			}
		}
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
