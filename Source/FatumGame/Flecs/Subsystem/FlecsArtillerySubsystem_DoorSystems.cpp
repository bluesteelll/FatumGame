// FlecsArtillerySubsystem - Door Systems
// TriggerUnlockSystem, DoorTickSystem

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsDoorComponents.h"
#include "FlecsStaticComponents.h"
#include "FlecsInstanceComponents.h"
#include "FBarragePrimitive.h"
#include "BarrageDispatch.h"

void UFlecsArtillerySubsystem::SetupDoorSystems()
{
	flecs::world& World = *FlecsWorld;

	// ─────────────────────────────────────────────────────────
	// TRIGGER UNLOCK SYSTEM
	// Watches trigger entities with FDoorTriggerLink.
	// When the trigger's FInteractionInstance.bToggleState becomes true,
	// resolves the linked door entity and sets bUnlocked = true.
	// Must run BEFORE DoorTickSystem so the door can react same tick.
	// NOTE: Writes FDoorInstance on a different entity via try_get_mut.
	// Safe because both systems are .each() in the same pipeline phase,
	// declared sequentially → Flecs runs them in declaration order.
	// ─────────────────────────────────────────────────────────
	World.system<const FDoorTriggerLink, const FInteractionInstance>("TriggerUnlockSystem")
		.with<FTagDoorTrigger>()
		.without<FTagDead>()
		.each([this](flecs::entity TriggerEntity, const FDoorTriggerLink& Link, const FInteractionInstance& Interaction)
		{
			EnsureBarrageAccess();

			if (!Interaction.bToggleState) return;
			if (!Link.IsValid()) return;

			// Resolve door entity: TargetDoorKey is the door's BarrageKey stored as uint64
			FSkeletonKey DoorKey(Link.TargetDoorKey);
			flecs::entity DoorEntity = GetEntityForBarrageKey(DoorKey);
			if (!DoorEntity.is_valid()) return;

			FDoorInstance* DoorInst = DoorEntity.try_get_mut<FDoorInstance>();
			if (DoorInst && !DoorInst->bUnlocked)
			{
				DoorInst->bUnlocked = true;
				UE_LOG(LogTemp, Log, TEXT("DOOR: Trigger %llu unlocked door %llu"),
					TriggerEntity.id(), DoorEntity.id());
			}
		});

	// ─────────────────────────────────────────────────────────
	// DOOR TICK SYSTEM
	// Main door state machine. Reads FInteractionInstance toggle
	// changes and drives the constraint motor to open/close.
	//
	// State flow:
	//   Locked → (bUnlocked) → Closed
	//   Closed → (toggle/push) → Opening → (at target) → Open
	//   Open → (toggle/push/auto-close) → Closing → (at zero) → Closed
	//   Opening ↔ Closing (toggle reverses mid-movement)
	//
	// bMotorDriven=true:  motor drives open/close
	// bMotorDriven=false: player pushes physically, motor only for auto-close/lock
	// ─────────────────────────────────────────────────────────
	World.system<FDoorInstance, const FDoorStatic, const FBarrageBody>("DoorTickSystem")
		.with<FTagDoor>()
		.without<FTagDead>()
		.each([this](flecs::entity Entity, FDoorInstance& Door, const FDoorStatic& Static, const FBarrageBody& Body)
		{
			EnsureBarrageAccess();

			if (!Door.HasConstraint() || !CachedBarrageDispatch)
				return;

			FBarrageConstraintKey CKey(static_cast<uint64>(Door.ConstraintKey));
			const float DeltaTime = Entity.world().get_info()->delta_time;

			// ── Read current constraint position ──
			float Current;
			if (Static.DoorType == EDoorType::Hinged)
				Current = CachedBarrageDispatch->GetConstraintCurrentAngle(CKey);
			else
				Current = CachedBarrageDispatch->GetConstraintCurrentPosition(CKey);

			// ── Detect toggle from interaction ──
			bool bToggled = false;
			const FInteractionInstance* Interaction = Entity.try_get<FInteractionInstance>();
			if (Interaction && Interaction->bToggleState != Door.bLastToggleState)
			{
				Door.bLastToggleState = Interaction->bToggleState;
				bToggled = true;
			}

			// ── Thresholds ──
			constexpr float AngleNearOpen = 0.02f;   // ~1 degree — responsive push detection
			constexpr float PosNearOpen = 0.01f;     // 1cm in Jolt meters
			constexpr float NearCloseThreshold = 0.02f;

			const float OpenThreshold = (Static.DoorType == EDoorType::Hinged) ? AngleNearOpen : PosNearOpen;

			// ── Mass-based latch helpers ──
			// Need FBarrageKey for SetBodyMass (FBarrageBody::BarrageKey is FSkeletonKey)
			FBLet DoorPrim = CachedBarrageDispatch->GetShapeRef(Body.BarrageKey);
			FBarrageKey DoorBarrageKey = DoorPrim ? DoorPrim->KeyIntoBarrage : FBarrageKey();

			auto SetHeavyMass = [&]()
			{
				if (DoorBarrageKey.KeyIntoBarrage != 0)
					CachedBarrageDispatch->SetBodyMass(DoorBarrageKey, Static.LockMass);
			};
			auto SetNormalMass = [&]()
			{
				if (DoorBarrageKey.KeyIntoBarrage != 0)
				{
					// Preserve momentum: scale angular velocity by mass ratio
					// so accumulated energy isn't lost on mass drop
					FVector3d AngVel = CachedBarrageDispatch->GetBodyAngularVelocity(DoorBarrageKey);
					float MassRatio = Static.LockMass / FMath::Max(Static.Mass, 1.f);
					CachedBarrageDispatch->SetBodyMass(DoorBarrageKey, Static.Mass);
					CachedBarrageDispatch->SetBodyAngularVelocity(DoorBarrageKey, AngVel * MassRatio);
				}
			};

			// ── Helper: start opening ──
			auto StartOpening = [&]()
			{
				Door.OpenDirection = 1;

				// Restore normal mass — door is no longer latched
				if (Static.bLockAtEndPosition)
					SetNormalMass();

				float Target;
				if (Static.DoorType == EDoorType::Hinged)
				{
					Target = Static.MaxOpenAngle * Door.OpenDirection;
					if (Static.bMotorDriven)
						CachedBarrageDispatch->SetConstraintTargetAngle(CKey, Target);
				}
				else
				{
					Target = Static.SlideDistance / 100.f;
					if (Static.bMotorDriven)
						CachedBarrageDispatch->SetConstraintTargetPosition(CKey, Target);
				}

				Door.TargetPosition = Target;

				if (Static.bMotorDriven)
					CachedBarrageDispatch->SetConstraintMotorState(CKey, 2); // Position
				else
					CachedBarrageDispatch->SetConstraintMotorState(CKey, 0); // Off — push-only

				Door.State = EDoorState::Opening;
			};

			// ── Helper: start closing ──
			auto StartClosing = [&]()
			{
				Door.TargetPosition = 0.f;

				// Restore normal mass — door is no longer latched
				if (Static.bLockAtEndPosition)
					SetNormalMass();

				// Motor drives closing only if motor-driven or auto-close enabled
				if (Static.bMotorDriven || Static.bAutoClose)
					CachedBarrageDispatch->SetConstraintMotorState(CKey, 2); // Position

				if (Static.DoorType == EDoorType::Hinged)
					CachedBarrageDispatch->SetConstraintTargetAngle(CKey, 0.f);
				else
					CachedBarrageDispatch->SetConstraintTargetPosition(CKey, 0.f);

				Door.State = EDoorState::Closing;
			};

			switch (Door.State)
			{
			case EDoorState::Locked:
			{
				// Unlock via external trigger OR interaction (E key, if allowed)
				if (Door.bUnlocked || (bToggled && Static.bUnlockOnInteraction))
				{
					Door.bUnlocked = true;
					Door.State = EDoorState::Closed;
					// Turn off immovable motor, restore normal torque limits
					CachedBarrageDispatch->SetConstraintMotorState(CKey, 0);
					CachedBarrageDispatch->SetConstraintMotorTorqueLimits(CKey, Static.MotorMaxTorque);
					// Closed is an end position — apply heavy mass if latching
					if (Static.bLockAtEndPosition)
						SetHeavyMass();
					UE_LOG(LogTemp, Log, TEXT("DOOR: Entity %llu unlocked -> Closed"), Entity.id());
				}
				break;
			}

			case EDoorState::Closed:
			{
				if (bToggled && Door.bUnlocked && Static.bMotorDriven)
				{
					StartOpening();
					UE_LOG(LogTemp, Log, TEXT("DOOR: Entity %llu Closed -> Opening (toggle)"), Entity.id());
				}
				else if (Door.bUnlocked)
				{
					// Detect physical push away from 0 (opening direction).
					// Friction naturally provides hysteresis — small forces absorbed, strong push overcomes.
					float Deflection = FMath::Abs(Current);
					if (Deflection > OpenThreshold)
					{
						StartOpening();
						UE_LOG(LogTemp, Log, TEXT("DOOR: Entity %llu Closed -> Opening (push=%.4f)"),
							Entity.id(), Deflection);
					}
				}
				break;
			}

			case EDoorState::Opening:
			{
				if (bToggled && Static.bMotorDriven)
				{
					StartClosing();
					UE_LOG(LogTemp, Log, TEXT("DOOR: Entity %llu Opening -> Closing (toggle reverse)"), Entity.id());
					break;
				}

				if (FMath::Abs(Current - Door.TargetPosition) < OpenThreshold)
				{
					Door.State = EDoorState::Open;
					Door.AutoCloseTimer = Static.AutoCloseDelay;

					// Latch: heavy mass resists movement via inertia
					if (Static.bLockAtEndPosition)
						SetHeavyMass();

					// Motor off — inertia (heavy mass) or angular damping holds position
					CachedBarrageDispatch->SetConstraintMotorState(CKey, 0);

					UE_LOG(LogTemp, Log, TEXT("DOOR: Entity %llu Opening -> Open (pos=%.3f)"), Entity.id(), Current);
				}
				else if (!Static.bMotorDriven && FMath::Abs(Current) < NearCloseThreshold)
				{
					// Push-only: door pushed back to closed
					Door.State = EDoorState::Closed;
					if (Static.bLockAtEndPosition)
						SetHeavyMass();
					UE_LOG(LogTemp, Log, TEXT("DOOR: Entity %llu Opening -> Closed (push returned)"), Entity.id());
				}
				break;
			}

			case EDoorState::Open:
			{
				if (bToggled)
				{
					StartClosing();
					UE_LOG(LogTemp, Log, TEXT("DOOR: Entity %llu Open -> Closing (toggle)"), Entity.id());
				}
				else if (Static.bLockAtEndPosition)
				{
					// Detect physical push toward closing (deflection from open target toward 0)
					float DeflectionTowardClose = FMath::Abs(Door.TargetPosition) - FMath::Abs(Current);
					if (DeflectionTowardClose > OpenThreshold)
					{
						StartClosing();
						UE_LOG(LogTemp, Log, TEXT("DOOR: Entity %llu Open -> Closing (push=%.4f)"),
							Entity.id(), DeflectionTowardClose);
					}
					else if (Static.bAutoClose)
					{
						Door.AutoCloseTimer -= DeltaTime;
						if (Door.AutoCloseTimer <= 0.f)
						{
							StartClosing();
							UE_LOG(LogTemp, Log, TEXT("DOOR: Entity %llu Open -> Closing (auto-close)"), Entity.id());
						}
					}
				}
				else if (!Static.bMotorDriven)
				{
					// Push-only (no lock): if door drifts back toward closed, track it
					if (FMath::Abs(Current) < NearCloseThreshold)
					{
						Door.State = EDoorState::Closed;
						UE_LOG(LogTemp, Log, TEXT("DOOR: Entity %llu Open -> Closed (push drift)"), Entity.id());
					}
					else if (Static.bAutoClose)
					{
						Door.AutoCloseTimer -= DeltaTime;
						if (Door.AutoCloseTimer <= 0.f)
						{
							StartClosing();
							UE_LOG(LogTemp, Log, TEXT("DOOR: Entity %llu Open -> Closing (auto-close)"), Entity.id());
						}
					}
				}
				else if (Static.bAutoClose)
				{
					Door.AutoCloseTimer -= DeltaTime;
					if (Door.AutoCloseTimer <= 0.f)
					{
						StartClosing();
						UE_LOG(LogTemp, Log, TEXT("DOOR: Entity %llu Open -> Closing (auto-close)"), Entity.id());
					}
				}
				break;
			}

			case EDoorState::Closing:
			{
				if (bToggled && Static.bMotorDriven)
				{
					StartOpening();
					UE_LOG(LogTemp, Log, TEXT("DOOR: Entity %llu Closing -> Opening (toggle reverse)"), Entity.id());
					break;
				}

				if (FMath::Abs(Current) < NearCloseThreshold)
				{
					Door.State = EDoorState::Closed;

					// Latch: heavy mass at end position
					if (Static.bLockAtEndPosition)
						SetHeavyMass();

					// Motor off — inertia holds position
					CachedBarrageDispatch->SetConstraintMotorState(CKey, 0);

					UE_LOG(LogTemp, Log, TEXT("DOOR: Entity %llu Closing -> Closed"), Entity.id());
				}
				break;
			}
			}
		});
}
