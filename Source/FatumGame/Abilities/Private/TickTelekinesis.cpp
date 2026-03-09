// Telekinesis ability tick function — constraint-based object manipulation.
// Sim-thread only. Grabs dynamic physics objects via a PointConstraint to a
// kinematic pivot body. Pivot follows crosshair; physics solver resolves
// surface contacts naturally (no jitter).

#include "AbilityTickFunctions.h"
#include "FlecsAbilityStates.h"
#include "FlecsAbilityDefinition.h"
#include "FlecsResourceTypes.h"
#include "FlecsGameTags.h"
#include "FlecsCharacterTypes.h"
#include "BarrageDispatch.h"
#include "BarrageConstraintSystem.h"
#include "FBarragePrimitive.h"
#include "EPhysicsLayer.h"
#include "PhysicsFilters/FastObjectLayerFilters.h"

// ── Helpers ──────────────────────────────────────────────────

static FVector ReadCamLoc(const FCharacterInputAtomics* Input)
{
	return FVector(Input->CamLocX.Read(), Input->CamLocY.Read(), Input->CamLocZ.Read());
}

static FVector ReadCamDir(const FCharacterInputAtomics* Input)
{
	return FVector(Input->CamDirX.Read(), Input->CamDirY.Read(), Input->CamDirZ.Read());
}

static FVector ComputeHoldPoint(const FVector& CamLoc, const FVector& CamDir,
	float HoldDistance, float VerticalOffset)
{
	return CamLoc + CamDir * HoldDistance + FVector(0.0, 0.0, VerticalOffset);
}

/** Exponentially smooth the hold point to prevent pivot velocity spikes.
 *  On first call (bSmoothedInit=false), snaps to target. */
static FVector SmoothHoldPoint(FTelekinesisState& State, const FVector& Target, float DT, float InterpSpeed)
{
	if (!State.bSmoothedInit)
	{
		State.SmoothedX = static_cast<float>(Target.X);
		State.SmoothedY = static_cast<float>(Target.Y);
		State.SmoothedZ = static_cast<float>(Target.Z);
		State.bSmoothedInit = true;
		return Target;
	}

	const float Alpha = FMath::Clamp(1.f - FMath::Exp(-InterpSpeed * DT), 0.f, 1.f);
	State.SmoothedX = FMath::Lerp(State.SmoothedX, static_cast<float>(Target.X), Alpha);
	State.SmoothedY = FMath::Lerp(State.SmoothedY, static_cast<float>(Target.Y), Alpha);
	State.SmoothedZ = FMath::Lerp(State.SmoothedZ, static_cast<float>(Target.Z), Alpha);
	return FVector(State.SmoothedX, State.SmoothedY, State.SmoothedZ);
}

static void DampAngularVelocity(UBarrageDispatch* Barrage, FBarrageKey Key, float Damping, float DT)
{
	FVector3d AngVel = Barrage->GetBodyAngularVelocity(Key);
	const double Factor = FMath::Exp(-static_cast<double>(Damping) * DT);
	Barrage->SetBodyAngularVelocity(Key, AngVel * Factor);
}

/** Clamp hold point so pivot never passes through walls or other objects.
 *  Casts ray from object → desired hold point. Ignores the grabbed body itself
 *  (ray starts at its center — Jolt skips backface hits on convex shapes anyway,
 *  but the body filter is a safety net). Excludes projectiles and debris layers. */
static FVector ClampHoldPointByRaycast(UBarrageDispatch* Barrage,
	const FVector& ObjPos, const FVector& DesiredHoldPoint,
	FBarrageKey GrabbedBarrageKey)
{
	const FVector ToHold = DesiredHoldPoint - ObjPos;
	const double Dist = ToHold.Size();
	if (Dist < 1.0) return DesiredHoldPoint;

	auto BPFilter = Barrage->GetDefaultBroadPhaseLayerFilter(Layers::CAST_QUERY);
	FastExcludeObjectLayerFilter ObjFilter({
		EPhysicsLayer::PROJECTILE,
		EPhysicsLayer::ENEMYPROJECTILE,
		EPhysicsLayer::DEBRIS
	});
	auto BodyFilter = Barrage->GetFilterToIgnoreSingleBody(GrabbedBarrageKey);

	TSharedPtr<FHitResult> Hit = MakeShared<FHitResult>();
	Barrage->CastRay(ObjPos, ToHold, BPFilter, ObjFilter, BodyFilter, Hit);

	if (Hit->bBlockingHit)
	{
		constexpr double WallOffset = 15.0; // cm — keep pivot on this side of the wall
		const FVector Dir = ToHold / Dist;
		const double HitDist = (Hit->ImpactPoint - ObjPos).Size();
		const double ClampedDist = FMath::Max(0.0, HitDist - WallOffset);
		return ObjPos + Dir * ClampedDist;
	}

	return DesiredHoldPoint;
}

/** Tear down constraint + pivot body, restore grabbed object state. */
void ReleaseTelekinesisObject(FTelekinesisState& State, FAbilityTickContext& Ctx,
	bool bThrow, const FTelekinesisConfig* Config, FAbilitySlot* Slot)
{
	if (State.Phase == 0) return;

	// Remove constraint first
	if (State.ConstraintKey.IsValid())
	{
		FBarrageConstraintSystem* CS = Ctx.Barrage->GetConstraintSystem();
		if (CS) CS->Remove(State.ConstraintKey);
	}

	// Destroy pivot body (safe: no FBLet/ISM, null-guarded body_interface)
	if (State.PivotBarrageKey.KeyIntoBarrage != 0)
	{
		Ctx.Barrage->FinalizeReleasePrimitive(State.PivotBarrageKey);
	}

	// Restore grabbed object
	FBLet Prim = Ctx.Barrage->GetShapeRef(State.GrabbedBarrageKey);
	if (FBarragePrimitive::IsNotNull(Prim))
	{
		FBarragePrimitive::SetGravityFactor(State.OriginalGravityFactor, Prim);

		if (bThrow && Config)
		{
			const FVector CamDir = ReadCamDir(Ctx.Input);
			FBarragePrimitive::SetVelocity(FVector3d(CamDir * Config->ThrowSpeed), Prim);
		}

		uint64 FlecsId = Prim->GetFlecsEntity();
		if (FlecsId != 0)
		{
			flecs::entity HeldEntity = Ctx.Entity.world().entity(FlecsId);
			if (HeldEntity.is_alive())
			{
				HeldEntity.remove<FTagTelekinesisHeld>();
			}
		}
	}

	State.Reset();
	if (Slot) Slot->Phase = 0;
}

/** Drain mana based on held object mass. Returns false if mana depleted (triggers release). */
static bool DrainMana(FTelekinesisState& State, FAbilityTickContext& Ctx,
	const FTelekinesisConfig* Config, float DT)
{
	FResourcePools* Pools = Ctx.Entity.try_get_mut<FResourcePools>();
	if (!Pools) return false;

	const float MassRatio = Config->ReferenceMass > 0.f
		? State.GrabbedMass / Config->ReferenceMass
		: 1.f;
	const float MassMultiplier = FMath::Pow(FMath::Max(MassRatio, 0.01f), Config->MassExponent);
	const float DrainAmount = Config->BaseDrainRate * MassMultiplier * DT;

	if (!Pools->CanAfford(EResourceTypeId::Mana, DrainAmount))
	{
		ReleaseTelekinesisObject(State, Ctx, false, Config, nullptr);
		return false;
	}

	Pools->Consume(EResourceTypeId::Mana, DrainAmount);
	return true;
}

/** Validate that the grabbed object and constraint are still alive. */
static bool ValidateGrabbedObject(FTelekinesisState& State, FAbilityTickContext& Ctx,
	const FTelekinesisConfig* Config, FAbilitySlot& Slot)
{
	FBLet Prim = Ctx.Barrage->GetShapeRef(State.GrabbedBarrageKey);
	if (!FBarragePrimitive::IsNotNull(Prim))
	{
		ReleaseTelekinesisObject(State, Ctx, false, Config, &Slot);
		return false;
	}

	uint64 FlecsId = Prim->GetFlecsEntity();
	if (FlecsId != 0)
	{
		flecs::entity HeldEntity = Ctx.Entity.world().entity(FlecsId);
		if (!HeldEntity.is_alive() || HeldEntity.has<FTagDead>())
		{
			ReleaseTelekinesisObject(State, Ctx, false, Config, &Slot);
			return false;
		}
	}

	// Constraint validity
	FBarrageConstraintSystem* CS = Ctx.Barrage->GetConstraintSystem();
	if (!CS || !CS->IsValid(State.ConstraintKey))
	{
		ReleaseTelekinesisObject(State, Ctx, false, Config, &Slot);
		return false;
	}

	return true;
}

// ── Main Tick ────────────────────────────────────────────────

EAbilityTickResult TickTelekinesis(FAbilityTickContext& Ctx, FAbilitySlot& Slot)
{
	check(Ctx.Input);
	check(Ctx.Barrage);

	auto* SlotData = reinterpret_cast<FTelekinesisSlotData*>(Slot.ConfigData);
	const FTelekinesisConfig* Config = SlotData->Config;
	checkf(Config, TEXT("TickTelekinesis: SlotData->Config is null — FromLoadout not called?"));

	FTelekinesisState* State = Ctx.Entity.try_get_mut<FTelekinesisState>();
	if (!ensure(State)) return EAbilityTickResult::Continue;

	const float DT = Ctx.DeltaTime;

	// Read input (consume one-shots regardless of phase to prevent stale events)
	const bool bToggle = Ctx.Input->TelekinesisToggle.Consume();
	const bool bThrow = Ctx.Input->TelekinesisThrow.Consume();
	const float ScrollDelta = Ctx.Input->TelekinesisScroll.Read();

	// Update hold distance from scroll
	if (FMath::Abs(ScrollDelta) > KINDA_SMALL_NUMBER)
	{
		SlotData->CurrentHoldDistance = FMath::Clamp(
			SlotData->CurrentHoldDistance + ScrollDelta * SlotData->ScrollSpeed,
			SlotData->MinHoldDistance,
			SlotData->MaxHoldDistance);
	}

	switch (State->Phase)
	{

	// ── IDLE ──────────────────────────────────────────────
	case 0:
	{
		if (!bToggle) return EAbilityTickResult::Continue;

		const FVector CamLoc = ReadCamLoc(Ctx.Input);
		const FVector CamDir = ReadCamDir(Ctx.Input);

		// Raycast to find target
		auto BPFilter = Ctx.Barrage->GetDefaultBroadPhaseLayerFilter(Layers::CAST_QUERY);
		FastExcludeObjectLayerFilter ObjFilter({
			EPhysicsLayer::PROJECTILE,
			EPhysicsLayer::ENEMYPROJECTILE,
			EPhysicsLayer::DEBRIS
		});
		FBarrageKey CharBarrageKey = Ctx.Barrage->GetBarrageKeyFromSkeletonKey(Ctx.CharacterKey);
		auto BodyFilter = Ctx.Barrage->GetFilterToIgnoreSingleBody(CharBarrageKey);

		TSharedPtr<FHitResult> Hit = MakeShared<FHitResult>();
		Ctx.Barrage->CastRay(CamLoc, CamDir * Config->MaxGrabRange,
			BPFilter, ObjFilter, BodyFilter, Hit);

		if (!Hit->bBlockingHit) return EAbilityTickResult::Continue;

		FBarrageKey HitBarrageKey = Ctx.Barrage->GetBarrageKeyFromFHitResult(Hit);
		FBLet Prim = Ctx.Barrage->GetShapeRef(HitBarrageKey);
		if (!FBarragePrimitive::IsNotNull(Prim)) return EAbilityTickResult::Continue;

		// Entity validity
		uint64 FlecsId = Prim->GetFlecsEntity();
		flecs::entity TargetEntity;
		if (FlecsId != 0)
		{
			TargetEntity = Ctx.Entity.world().entity(FlecsId);
			if (!TargetEntity.is_alive() || TargetEntity.has<FTagDead>())
				return EAbilityTickResult::Continue;
			if (TargetEntity.has<FTagTelekinesisHeld>())
				return EAbilityTickResult::Continue;
		}

		// Mass check (also confirms dynamic body — returns 0 for non-dynamic)
		float Mass = Ctx.Barrage->GetBodyMass(HitBarrageKey);
		if (Mass <= 0.f || Mass > Config->MaxGrabbableMass)
			return EAbilityTickResult::Continue;

		// Activation cost
		{
			FResourcePools* Pools = Ctx.Entity.try_get_mut<FResourcePools>();
			if (Pools && Slot.HasActivationCosts())
			{
				for (int32 c = 0; c < Slot.ActivationCostCount; ++c)
				{
					const auto& Cost = Slot.ActivationCosts[c];
					if (Cost.IsValid() && !Pools->CanAfford(Cost.ResourceType, Cost.Amount))
						return EAbilityTickResult::Continue;
				}
				for (int32 c = 0; c < Slot.ActivationCostCount; ++c)
				{
					const auto& Cost = Slot.ActivationCosts[c];
					if (Cost.IsValid()) Pools->Consume(Cost.ResourceType, Cost.Amount);
				}
			}
		}

		// ── Create kinematic pivot at object position ──
		const FVector ObjPos(FBarragePrimitive::GetPosition(Prim));
		FBarrageKey PivotKey = Ctx.Barrage->CreateKinematicPivot(ObjPos);
		if (PivotKey.KeyIntoBarrage == 0)
			return EAbilityTickResult::Continue;

		// ── Create PointConstraint: pivot (Body1) ↔ object (Body2) ──
		// PointConstraint locks all 3 position axes (no orbiting unlike DistanceConstraint).
		// Hold point smoothing in Phase 1/2 prevents velocity spikes that cause oscillation.
		FBarrageConstraintSystem* CS = Ctx.Barrage->GetConstraintSystem();
		if (!CS)
		{
			Ctx.Barrage->FinalizeReleasePrimitive(PivotKey);
			return EAbilityTickResult::Continue;
		}

		FBPointConstraintParams PointParams;
		PointParams.Body1 = PivotKey;
		PointParams.Body2 = HitBarrageKey;
		PointParams.Space = EBConstraintSpace::LocalToBody;
		PointParams.bAutoDetectAnchor = false;
		PointParams.AnchorPoint1 = FVector3d::ZeroVector;
		PointParams.AnchorPoint2 = FVector3d::ZeroVector;

		FBarrageConstraintKey CKey = CS->CreatePoint(PointParams);
		if (!CKey.IsValid())
		{
			Ctx.Barrage->FinalizeReleasePrimitive(PivotKey);
			return EAbilityTickResult::Continue;
		}

		// ── Commit grab state ──
		State->GrabbedKey = Prim->KeyOutOfBarrage;
		State->GrabbedBarrageKey = HitBarrageKey;
		State->PivotBarrageKey = PivotKey;
		State->ConstraintKey = CKey;
		State->GrabbedMass = Mass;
		State->OriginalGravityFactor = Ctx.Barrage->GetBodyGravityFactor(HitBarrageKey);
		State->StuckTimer = 0.f;
		State->AcquireTimer = 0.f;
		State->Phase = 1;

		FBarragePrimitive::SetGravityFactor(0.f, Prim);

		if (TargetEntity.is_valid() && TargetEntity.is_alive())
		{
			TargetEntity.add<FTagTelekinesisHeld>();
		}

		Slot.Phase = 1;
		return EAbilityTickResult::Continue;
	}

	// ── ACQUIRING ─────────────────────────────────────────
	case 1:
	{
		if (!ValidateGrabbedObject(*State, Ctx, Config, Slot))
			return EAbilityTickResult::Continue;

		if (bToggle)
		{
			ReleaseTelekinesisObject(*State, Ctx, false, Config, &Slot);
			return EAbilityTickResult::Continue;
		}

		FBLet Prim = Ctx.Barrage->GetShapeRef(State->GrabbedBarrageKey);
		if (!FBarragePrimitive::IsNotNull(Prim))
		{
			ReleaseTelekinesisObject(*State, Ctx, false, Config, &Slot);
			return EAbilityTickResult::Continue;
		}

		const FVector CamLoc = ReadCamLoc(Ctx.Input);
		const FVector CamDir = ReadCamDir(Ctx.Input);
		const FVector DesiredHoldPoint = ComputeHoldPoint(CamLoc, CamDir,
			SlotData->CurrentHoldDistance, Config->VerticalHoldOffset);

		const FVector ObjPos(FBarragePrimitive::GetPosition(Prim));
		const FVector ClampedPoint = ClampHoldPointByRaycast(Ctx.Barrage,
			ObjPos, DesiredHoldPoint, State->GrabbedBarrageKey);
		const FVector HoldPoint = SmoothHoldPoint(*State, ClampedPoint, DT, Config->HoldPointInterpSpeed);

		// Move pivot toward smoothed+clamped hold point — no velocity spikes
		Ctx.Barrage->MoveKinematicBody(State->PivotBarrageKey, HoldPoint, DT);

		DampAngularVelocity(Ctx.Barrage, State->GrabbedBarrageKey, Config->AngularDamping, DT);

		if (!DrainMana(*State, Ctx, Config, DT))
			return EAbilityTickResult::Continue;

		// Check arrival — transition to HOLDING or timeout
		const double Distance = (HoldPoint - ObjPos).Size();
		if (Distance < 50.0)
		{
			State->Phase = 2;
			State->StuckTimer = 0.f;
		}
		else
		{
			State->AcquireTimer += DT;
			if (State->AcquireTimer > Config->AcquireTimeout)
			{
				ReleaseTelekinesisObject(*State, Ctx, false, Config, &Slot);
			}
		}

		return EAbilityTickResult::Continue;
	}

	// ── HOLDING ───────────────────────────────────────────
	case 2:
	{
		if (!ValidateGrabbedObject(*State, Ctx, Config, Slot))
			return EAbilityTickResult::Continue;

		if (bToggle)
		{
			ReleaseTelekinesisObject(*State, Ctx, false, Config, &Slot);
			return EAbilityTickResult::Continue;
		}

		if (bThrow)
		{
			ReleaseTelekinesisObject(*State, Ctx, true, Config, &Slot);
			return EAbilityTickResult::Continue;
		}

		FBLet Prim = Ctx.Barrage->GetShapeRef(State->GrabbedBarrageKey);
		if (!FBarragePrimitive::IsNotNull(Prim))
		{
			ReleaseTelekinesisObject(*State, Ctx, false, Config, &Slot);
			return EAbilityTickResult::Continue;
		}

		const FVector CamLoc = ReadCamLoc(Ctx.Input);
		const FVector CamDir = ReadCamDir(Ctx.Input);
		const FVector DesiredHoldPoint = ComputeHoldPoint(CamLoc, CamDir,
			SlotData->CurrentHoldDistance, Config->VerticalHoldOffset);

		const FVector ObjPos(FBarragePrimitive::GetPosition(Prim));
		const FVector ClampedPoint = ClampHoldPointByRaycast(Ctx.Barrage,
			ObjPos, DesiredHoldPoint, State->GrabbedBarrageKey);
		const FVector HoldPoint = SmoothHoldPoint(*State, ClampedPoint, DT, Config->HoldPointInterpSpeed);

		// Move pivot — smoothed+clamped so no velocity spikes and no wall penetration
		Ctx.Barrage->MoveKinematicBody(State->PivotBarrageKey, HoldPoint, DT);

		DampAngularVelocity(Ctx.Barrage, State->GrabbedBarrageKey, Config->AngularDamping, DT);

		if (!DrainMana(*State, Ctx, Config, DT))
			return EAbilityTickResult::Continue;

		// Stuck auto-release (use distance to DESIRED hold point, not clamped)
		const double Distance = (DesiredHoldPoint - ObjPos).Size();

		if (Distance > 100.0)
		{
			State->StuckTimer += DT;
		}
		else
		{
			State->StuckTimer = FMath::Max(0.f, State->StuckTimer - DT * 2.f);
		}

		if (State->StuckTimer > Config->MaxStuckTime)
		{
			ReleaseTelekinesisObject(*State, Ctx, false, Config, &Slot);
		}

		return EAbilityTickResult::Continue;
	}

	default:
		State->Reset();
		Slot.Phase = 0;
		return EAbilityTickResult::Continue;
	}
}
