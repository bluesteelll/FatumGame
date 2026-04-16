// FlecsArtillerySubsystem - MeleeSweepSystem (Phase 5 — HOT PATH)
// Runs during EMeleeAttackPhase::Release. Sweeps an oriented capsule from the
// previous blade pose to the current blade pose (substepped via slerp) and
// resolves each hit via UFlecsBulletHitLibrary::ApplyBulletHit.
//
// Source dispatch (blueprint N-C3):
//   BladeBuffer != nullptr → drain per-weapon triple buffer (player / skeletal AI).
//   BladeBuffer == nullptr → read FBladeSocketSync component (simple-AI procedural).
//
// Stale-frame handling (blueprint C5 / N-M3): if FrameStamp is 0 or unchanged
// from the last sweep, SKIP — do NOT touch PrevBlade. Only LastSweepFrameStamp
// is stamped so the next fresh frame yields a genuine delta.
//
// Body filter: IgnoreSingleBodyFilter(attackerCharacterBodyID) — equipped melee
// weapons have no independent Jolt body (blueprint M10).
//
// System ordering: ... → MeleePhaseAdvanceSystem → MeleeSweepSystem → ...

#include "FlecsArtillerySubsystem.h"
#include "FlecsGameTags.h"
#include "FlecsMeleeComponents.h"
#include "FlecsWeaponComponents.h"             // FEquippedBy
#include "FlecsBarrageComponents.h"            // FBarrageBody
#include "FlecsPenetrationComponents.h"        // FPenetrationMaterial
#include "Library/FlecsBulletHitLibrary.h"

#include "BarrageDispatch.h"
#include "FBarragePrimitive.h"
#include "FBarrageShapeHit.h"
#include "IsolatedJoltIncludes.h"
#include "EPhysicsLayer.h"
#include "PhysicsFilters/FastObjectLayerFilters.h"

namespace
{
	/** Single-body exclusion filter (mirrors FShooterExcludeFilter in FlecsHitscanLibrary).
	 *  Inline allocator — one attacker body in practice. */
	class FAttackerExcludeFilter final : public JPH::BodyFilter
	{
	public:
		TArray<JPH::BodyID, TInlineAllocator<2>> Excluded;

		virtual bool ShouldCollide(const JPH::BodyID& inBodyID) const override
		{
			for (const JPH::BodyID& E : Excluded)
			{
				if (inBodyID == E) return false;
			}
			return true;
		}

		virtual bool ShouldCollideLocked(const JPH::Body& /*inBody*/) const override
		{
			return true;
		}
	};

	/** Reference axis for building blade-rotation quaternions. Blade segment direction
	 *  is mapped onto this axis via FindBetweenNormals, so the quaternion encodes the
	 *  blade's world-space orientation without needing a full look-at frame. */
	// Jolt's CapsuleShape is Y-axis-aligned in its local space. Our CoordinateUtils::ToJoltRotation
	// swaps axes (X,Y,Z)_UE → (X,Z,Y)_Jolt, so UE-Z (0,0,1) maps to Jolt-Y — Jolt's native capsule
	// axis. Building the orientation quat against UE-Z is therefore correct — after the swap the
	// capsule traces along the blade in Jolt world space. (Component negation in ToJoltRotation
	// produces -Q which represents the same SO(3) rotation — no effect on capsule orientation.)
	const FVector kBladeReferenceAxis(0.f, 0.f, 1.f);

	/** Resolve a Jolt BodyID to a flecs entity using the hitscan pattern:
	 *  BodyID → FBarrageKey → FBLet → FlecsEntityId → entity. Returns invalid entity
	 *  for static geometry or stale keys. */
	flecs::entity ResolveBodyToEntity(flecs::world& World, UBarrageDispatch* Barrage, uint32 BodyIDValue)
	{
		const FBarrageKey HitKey = Barrage->GenerateBarrageKeyFromBodyId(BodyIDValue);
		const FBLet HitPrim = Barrage->GetShapeRef(HitKey);
		if (!FBarragePrimitive::IsNotNull(HitPrim)) return flecs::entity();
		const uint64 FlecsId = HitPrim->GetFlecsEntity();
		if (FlecsId == 0) return flecs::entity();
		return World.entity(FlecsId);
	}
}

void UFlecsArtillerySubsystem::SetupMeleeSweepSystem()
{
	flecs::world& World = *FlecsWorld;

	World.system<FMeleeWeaponInstance, const FMeleeWeaponStatic, const FEquippedBy>("MeleeSweepSystem")
		.with<FTagMeleeAttacking>()
		.without<FTagDead>()
		.each([this, &World](flecs::entity WeaponEntity,
			FMeleeWeaponInstance& Inst,
			const FMeleeWeaponStatic& Static,
			const FEquippedBy& EquippedBy)
		{
			// Hot path: register thread for Barrage exactly once per worker.
			EnsureBarrageAccess();

			// ── Gate 1: only sweep during Release. ──
			if (Inst.Phase != EMeleeAttackPhase::Release) return;
			if (!EquippedBy.IsEquipped()) return;
			if (!CachedBarrageDispatch) return;

			// ── Step 1: Source dispatch (N-C3). ──
			FVector SnapshotStart      = FVector::ZeroVector;
			FVector SnapshotTip        = FVector::ZeroVector;
			uint64  SnapshotFrameStamp = 0;

			if (Inst.BladeBuffer != nullptr)
			{
				// Player / full-AI path: drain triple buffer.
				const FMeleeWeaponInstance::FBladeSocketData& Sample = Inst.BladeBuffer->Read();
				SnapshotStart      = Sample.Start;
				SnapshotTip        = Sample.Tip;
				SnapshotFrameStamp = Sample.FrameStamp;
			}
			else
			{
				// Simple-AI path: read FBladeSocketSync component written by AI driver.
				const FBladeSocketSync* Sync = WeaponEntity.try_get<FBladeSocketSync>();
				if (!Sync) return;  // AI driver not yet wired — silently skip.
				SnapshotStart      = Sync->BladeStartWS;
				SnapshotTip        = Sync->BladeTipWS;
				SnapshotFrameStamp = Sync->FrameStamp;
			}

			// ── Step 2: Stale-frame / first-tick handling (C5 / N-M3 / B1). ──
			// SnapshotFrameStamp == 0 → game thread never wrote this swing yet → skip, stamp only LastSweep.
			// == LastSweep → same sample seen last tick (writer stalled) → skip, preserve Prev for recovery.
			if (SnapshotFrameStamp == 0 || SnapshotFrameStamp == Inst.LastSweepFrameStamp)
			{
				Inst.LastSweepFrameStamp = SnapshotFrameStamp;
				return;
			}

			// LastSweepFrameStamp == 0 is the unambiguous first-valid-tick signal (SwingInit
			// zeros it per blueprint C5). Seed Prev = Cur and stamp — no sweep this tick
			// (no meaningful delta yet). Next tick computes real (Cur - seeded-Prev) delta.
			if (Inst.LastSweepFrameStamp == 0)
			{
				Inst.PrevBladeStartWS    = SnapshotStart;
				Inst.PrevBladeTipWS      = SnapshotTip;
				Inst.LastSweepFrameStamp = SnapshotFrameStamp;
				return;
			}

			const float DeltaTime = WeaponEntity.world().get_info()->delta_time;
			if (DeltaTime <= KINDA_SMALL_NUMBER) return;

			// ── Step 3: tip speed. ──
			const float TipSpeed = (SnapshotTip - Inst.PrevBladeTipWS).Size() / DeltaTime;
			Inst.LastTipSpeed = TipSpeed;

			// Incoming direction for ApplyBulletHit — unit vector of tip's motion.
			FVector IncomingDir = (SnapshotTip - Inst.PrevBladeTipWS).GetSafeNormal();
			if (IncomingDir.IsNearlyZero())
			{
				// Blade not moving this tick — no meaningful impact direction. Stamp + exit.
				Inst.PrevBladeStartWS    = SnapshotStart;
				Inst.PrevBladeTipWS      = SnapshotTip;
				Inst.LastSweepFrameStamp = SnapshotFrameStamp;
				return;
			}

			// ── Step 4: resolve attacker character body for filter + SpawnPosition. ──
			flecs::entity AttackerEntity = World.entity(static_cast<uint64>(EquippedBy.CharacterEntityId));
			if (!AttackerEntity.is_valid() || !AttackerEntity.is_alive()) return;

			FAttackerExcludeFilter AttackerFilter;
			FVector AttackerChestPos = FVector::ZeroVector;  // Used for distance-falloff SpawnPosition.
			{
				const FBarrageBody* AttackerBody = AttackerEntity.try_get<FBarrageBody>();
				if (AttackerBody && AttackerBody->IsValid())
				{
					const FBarrageKey BKey = CachedBarrageDispatch->GetBarrageKeyFromSkeletonKey(AttackerBody->BarrageKey);
					if (BKey.KeyIntoBarrage != 0)
					{
						const JPH::BodyID Id = CachedBarrageDispatch->GetJoltBodyID(BKey);
						if (!Id.IsInvalid())
						{
							AttackerFilter.Excluded.Add(Id);
						}

						// Chest position ≈ attacker body centre — good enough for distance-falloff.
						const FBLet AttackerPrim = CachedBarrageDispatch->GetShapeRef(BKey);
						if (FBarragePrimitive::IsNotNull(AttackerPrim))
						{
							AttackerChestPos = FVector(FBarragePrimitive::GetPosition(AttackerPrim));
						}
					}
				}
			}

			// ── Step 5: broad-phase + object filters (shared with hitscan). ──
			auto BPFilter = CachedBarrageDispatch->GetDefaultBroadPhaseLayerFilter(Layers::CAST_QUERY);
			FastExcludeObjectLayerFilter ObjFilter({
				EPhysicsLayer::PROJECTILE,
				EPhysicsLayer::ENEMYPROJECTILE,
				EPhysicsLayer::DEBRIS
			});

			// ── Step 6: substep sweep (M2). ──
			const int32 SubstepCount = FMath::Clamp(Static.SweepSubstepsPerTick, 1, 8);
			const float ReachCM      = FMath::Max(Static.Reach, 1.f);
			// HalfHeight is the cylindrical portion half-length (blueprint C.3 says "cylindrical
			// portion half-height"). For a blade capsule that spans [start, tip] = Reach total,
			// the cylindrical portion is (Reach - 2*Radius) — distribute by 0.5.
			const float HalfHeight = FMath::Max(0.01f, (ReachCM - 2.f * Static.CapsuleRadius) * 0.5f);

			const FVector PrevAxis = (Inst.PrevBladeTipWS - Inst.PrevBladeStartWS).GetSafeNormal();
			const FVector CurAxis  = (SnapshotTip - SnapshotStart).GetSafeNormal();
			// Degenerate (zero-length segment) → fall back to reference axis so the quaternion
			// is well-defined. Won't match blade pose but sweep volume is still sane.
			const FVector SafePrevAxis = PrevAxis.IsNearlyZero() ? kBladeReferenceAxis : PrevAxis;
			const FVector SafeCurAxis  = CurAxis.IsNearlyZero()  ? kBladeReferenceAxis : CurAxis;

			const FQuat QPrev = FQuat::FindBetweenNormals(kBladeReferenceAxis, SafePrevAxis);
			const FQuat QCur  = FQuat::FindBetweenNormals(kBladeReferenceAxis, SafeCurAxis);

			// Aggregate hits across substeps. Each substep's hits go through dedup before
			// landing here — duplicate bodies skip ApplyBulletHit.
			TArray<FBarrageShapeHit> SubstepHits;
			SubstepHits.Reserve(16);

			for (int32 i = 1; i <= SubstepCount; ++i)
			{
				const float tPrev = static_cast<float>(i - 1) / static_cast<float>(SubstepCount);
				const float tCur  = static_cast<float>(i)     / static_cast<float>(SubstepCount);
				const float tMid  = (tPrev + tCur) * 0.5f;

				const FQuat    QMid       = FQuat::Slerp(QPrev, QCur, tMid);
				const FVector  SubStartA  = FMath::Lerp(Inst.PrevBladeStartWS, SnapshotStart, tPrev);
				const FVector  SubStartB  = FMath::Lerp(Inst.PrevBladeStartWS, SnapshotStart, tCur);
				// Capsule CENTRE offset from the hilt along the rotated reference axis by half the
				// blade length. Matches the HalfHeight convention (cylindrical-portion halved).
				const FVector  RotatedRef = QMid.RotateVector(kBladeReferenceAxis);
				const FVector  CenterA    = SubStartA + RotatedRef * (ReachCM * 0.5f);
				const FVector  CenterB    = SubStartB + RotatedRef * (ReachCM * 0.5f);

				SubstepHits.Reset();
				CachedBarrageDispatch->CastCapsuleAllHits(
					CenterA, CenterB, QMid,
					HalfHeight, Static.CapsuleRadius,
					BPFilter, ObjFilter, AttackerFilter,
					SubstepHits);

				// ── Per-hit loop (sorted near→far by Fraction). ──
				for (const FBarrageShapeHit& Hit : SubstepHits)
				{
					flecs::entity Target = ResolveBodyToEntity(World, CachedBarrageDispatch, Hit.BodyIDValue);

					// Static geometry (no flecs entity): blade still sweeps through, but we
					// can't damage it. Skip rather than break — other targets in the arc
					// (e.g. an enemy behind a thin prop) should still register.
					if (!Target.is_valid() || !Target.is_alive()) continue;
					if (Target.has<FTagDead>()) continue;

					// ── Dedup (M1): linear search HitTargetIds. ──
					const uint64 TargetId = Target.id();
					bool bAlreadyHit = false;
					for (int32 h = 0; h < Inst.HitCount; ++h)
					{
						if (Inst.HitTargetIds[h] == TargetId) { bAlreadyHit = true; break; }
					}
					if (bAlreadyHit) continue;

					if (Inst.HitCount >= FMeleeWeaponInstance::MaxHitsPerSwing)
					{
						UE_LOG(LogTemp, Verbose,
							TEXT("MeleeSweep: hit cap %d reached on entity=%lld; ignoring further hits this swing"),
							FMeleeWeaponInstance::MaxHitsPerSwing, static_cast<int64>(WeaponEntity.id()));
						break;  // stop this substep; cap also applies to later substeps (carried in HitCount).
					}
					Inst.HitTargetIds[Inst.HitCount++] = TargetId;

					// ── Material rebound check (one-shot per swing). ──
					if (!Inst.bSwingRebounded
						&& Static.DeliveryType == EMeleeDamageDelivery::Slashing)
					{
						if (const FPenetrationMaterial* Mat = Target.try_get<FPenetrationMaterial>())
						{
							if (Mat->MaterialCategory == EPenetrationMaterialCategory::Metal
								|| Mat->MaterialCategory == EPenetrationMaterialCategory::Armor_Plate)
							{
								Inst.bSwingRebounded = true;
								// Recovery extension is applied at Release→Recovery entry
								// by MeleePhaseAdvanceSystem (reads bSwingRebounded).
							}
						}
					}

					// ── Build FBulletHitContext (§F.1). ──
					const float TipSpeedMul = FMath::Clamp(
						TipSpeed / FMath::Max(Static.ReferenceTipSpeed, 1.f),
						Static.TipSpeedMulMin,
						Static.TipSpeedMulMax);

					FBulletHitContext Ctx;
					Ctx.ShooterEntityId = AttackerEntity.id();
					Ctx.TargetEntityId  = TargetId;
					Ctx.ImpactPoint     = Hit.ImpactPoint;
					Ctx.ImpactNormal    = Hit.ImpactNormal;
					Ctx.IncomingDir     = IncomingDir;
					Ctx.SpawnPosition   = AttackerChestPos;  // distance falloff anchor
					Ctx.IncomingSpeed   = TipSpeed;

					Ctx.BaseDamage     = Static.BaseDamage * Inst.LatchedPayload.DamageMul * TipSpeedMul;
					Ctx.CritChance     = Static.CritChance;
					Ctx.CritMultiplier = Static.CritMultiplier;
					// DamageType tag: default-empty for MVP. Future mapping from Static.DeliveryType
					// to project gameplay tags can populate Ctx.DamageType here.

					Ctx.SubShapeIDValue = Hit.SubShapeIDValue;
					Ctx.PenStatic       = &Static.MeleePen;
					Ctx.bApplyImpulse   = true;
					Ctx.ImpulseStrength = Static.BaseImpulse
						* Inst.LatchedPayload.ImpulseMul
						* (TipSpeed / FMath::Max(Static.ReferenceTipSpeed, 1.f));
					Ctx.bCanDegrade     = true;

					// Per-swing penetration state pointers — blueprint C3 (NOT thread_local).
					Ctx.InOutRemainingBudget         = &Inst.PenState.RemainingBudget;
					Ctx.InOutCurrentDamageMultiplier = &Inst.PenState.CurrentDamageMultiplier;
					Ctx.InOutPenetrationCount        = &Inst.PenState.PenetrationCount;
					Ctx.InOutLastPenetratedTargetId  = &Inst.PenState.LastPenetratedTargetId;

					// TODO(Phase 6): check FTagMeleeBlocking on Target → build FPendingBlockAbsorb
					// and attenuate Ctx.BaseDamage BEFORE ApplyBulletHit (blueprint §F.1).

					UFlecsBulletHitLibrary::ApplyBulletHit(
						World, CachedBarrageDispatch,
						AttackerEntity, Target, Ctx);
				}

				if (Inst.HitCount >= FMeleeWeaponInstance::MaxHitsPerSwing) break;
			}

			// ── Step 7: update sweep state. ──
			Inst.PrevBladeStartWS    = SnapshotStart;
			Inst.PrevBladeTipWS      = SnapshotTip;
			Inst.LastSweepFrameStamp = SnapshotFrameStamp;
		});
}
