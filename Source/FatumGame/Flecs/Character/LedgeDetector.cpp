// Stateless 5-phase ledge detection algorithm using Barrage (Jolt) raycasts.
// Extracted from UMantleAbility::PerformDetection for reuse and testability.

#include "LedgeDetector.h"
#include "FlecsMovementProfile.h"
#include "BarrageDispatch.h"
#include "PhysicsFilters/FastObjectLayerFilters.h"
#include "EPhysicsLayer.h"

void FLedgeDetector::Detect(const FVector& CharFeetPos, const FVector& LookDir,
                             float CapsuleRadius, float CapsuleHalfHeight,
                             float MaxReachHeight, const UFlecsMovementProfile* Profile,
                             UBarrageDispatch* Barrage, FSkeletonKey CharBarrageKey,
                             FLedgeCandidate& OutCandidate)
{
	OutCandidate.bValid = false;

	if (!Profile || !Barrage) return;

	// ── Set up filters ──
	// Include static geometry + dynamic objects (active debris is MOVING layer).
	// MOVING also hits items/characters, but 5-phase validation rejects false positives.
	FastIncludeObjectLayerFilter LedgeFilter({EPhysicsLayer::NON_MOVING, EPhysicsLayer::MOVING});

	// Broad phase filter for CAST_QUERY layer
	auto BPFilter = Barrage->GetDefaultBroadPhaseLayerFilter(Layers::CAST_QUERY);

	// Ignore own character body
	FBarrageKey BodyKey = Barrage->GetBarrageKeyFromSkeletonKey(CharBarrageKey);
	auto BodyFilter = Barrage->GetFilterToIgnoreSingleBody(BodyKey);

	// Horizontal look direction (no vertical component)
	FVector HorizLook = FVector(LookDir.X, LookDir.Y, 0.f).GetSafeNormal();
	if (HorizLook.IsNearlyZero()) return;

	TSharedPtr<FHitResult> HitResult = MakeShared<FHitResult>();

	// CRITICAL: SphereCast Radius is in Jolt meters, NOT UE cm
	double JoltDetectionRadius = Profile->LedgeGrabDetectionRadius / 100.0;

	// ═══════════════════════════════════════════════════════════════
	// PHASE 1: FORWARD WALL CHECK (SphereCast)
	// Origin: character chest height
	// Direction: camera forward projected onto horizontal plane
	// ═══════════════════════════════════════════════════════════════

	FVector ChestOrigin = CharFeetPos;
	ChestOrigin.Z += Profile->StandingEyeHeight * 0.5f;

	Barrage->SphereCast(
		JoltDetectionRadius,
		Profile->MantleForwardReach,
		ChestOrigin,
		HorizLook,
		HitResult,
		BPFilter, LedgeFilter, BodyFilter);

	if (!HitResult->bBlockingHit) return;

	FVector WallHitPoint = HitResult->ImpactPoint;
	FVector WallNormal = HitResult->ImpactNormal;

	// Validate wall normal is roughly horizontal (not floor/ceiling)
	if (FMath::Abs(WallNormal.Z) > 0.5f) return;

	// Ensure wall normal points away from character
	WallNormal = FVector(WallNormal.X, WallNormal.Y, 0.f).GetSafeNormal();
	if (WallNormal.IsNearlyZero()) return;

	// Facing check: must be looking toward the wall
	float FacingDot = FVector::DotProduct(HorizLook, -WallNormal);
	if (FacingDot < 0.3f) return;

	// ═══════════════════════════════════════════════════════════════
	// PHASE 2: HIGH FORWARD PROBE (SphereCast)
	// Cast forward at MaxReachHeight above feet to check if wall
	// extends that high. Origin is at CHARACTER position (guaranteed
	// outside any wall body). If hit → wall too tall → reject.
	// If no hit → wall shorter → Phase 3 downward cast is safe.
	// ═══════════════════════════════════════════════════════════════

	// Margin above MaxReachHeight: compensates for SphereCast radius + boundary precision.
	// Phase 3 height validation enforces the actual MantleMaxHeight limit precisely.
	const float ProbeMargin = 15.f; // cm

	FVector Phase2Origin = CharFeetPos;
	Phase2Origin.Z += MaxReachHeight + ProbeMargin;

	HitResult = MakeShared<FHitResult>();

	Barrage->SphereCast(
		JoltDetectionRadius,
		Profile->MantleForwardReach,
		Phase2Origin,
		HorizLook,
		HitResult,
		BPFilter, LedgeFilter, BodyFilter);

	if (HitResult->bBlockingHit) return;

	// ═══════════════════════════════════════════════════════════════
	// PHASE 3: DOWNWARD SURFACE (SphereCast)
	// Find the ledge top surface. Origin is 15cm into the wall face
	// at MaxReachHeight+margin Z — guaranteed ABOVE the wall body by Phase 2.
	// ═══════════════════════════════════════════════════════════════

	FVector Phase3Origin = WallHitPoint - WallNormal * 15.f; // 15cm into wall cross-section
	Phase3Origin.Z = CharFeetPos.Z + MaxReachHeight + ProbeMargin; // above the wall (Phase 2 confirmed)

	float Phase3Distance = MaxReachHeight + ProbeMargin - Profile->MantleMinHeight;
	if (Phase3Distance <= 0.f) return;

	// Small sphere for surface detection (5cm radius in Jolt meters)
	double SmallRadius = 5.0 / 100.0;

	HitResult = MakeShared<FHitResult>();
	Barrage->SphereCast(
		SmallRadius,
		Phase3Distance,
		Phase3Origin,
		-FVector::UpVector, // straight down
		HitResult,
		BPFilter, LedgeFilter, BodyFilter);

	if (!HitResult->bBlockingHit) return;

	FVector LedgeTopPoint = HitResult->ImpactPoint;
	FVector LedgeNormal = HitResult->ImpactNormal;

	// Validate ledge surface is roughly flat (slope < ~45 degrees)
	if (FVector::DotProduct(LedgeNormal, FVector::UpVector) < 0.7f) return;

	// Validate ledge height is within acceptable range
	float LedgeHeight = LedgeTopPoint.Z - CharFeetPos.Z;
	if (LedgeHeight < Profile->MantleMinHeight || LedgeHeight > MaxReachHeight) return;

	// ── Look angle gate (relative to ledge top) ──
	// Reject if the angle between LookDir and the direction from eye to ledge top
	// exceeds the configured threshold. Prevents activation when looking at the floor.
	FVector EyePos = CharFeetPos;
	EyePos.Z += Profile->StandingEyeHeight;
	FVector ToLedge = (LedgeTopPoint - EyePos).GetSafeNormal();
	float LookToLedgeDot = FVector::DotProduct(LookDir, ToLedge);
	float MinDot = FMath::Cos(FMath::DegreesToRadians(Profile->LedgeDetectMaxLookDownAngle));
	if (LookToLedgeDot < MinDot) return;

	// ═══════════════════════════════════════════════════════════════
	// PHASE 4: DEPTH CHECK (CastRay)
	// Ensure the ledge has enough depth to stand on
	// ═══════════════════════════════════════════════════════════════

	FVector Phase4Origin = LedgeTopPoint + FVector(0, 0, 5.f); // slightly above surface
	// Direction = into wall (opposite of wall normal) * min depth distance
	FVector DepthDir = -WallNormal * Profile->LedgeGrabMinLedgeDepth;

	HitResult = MakeShared<FHitResult>();
	Barrage->CastRay(
		Phase4Origin,
		DepthDir,
		BPFilter, LedgeFilter, BodyFilter,
		HitResult);

	// If hit, ledge is too thin — reject
	if (HitResult->bBlockingHit) return;

	// ═══════════════════════════════════════════════════════════════
	// PHASE 5: TOP CLEARANCE (CastRay)
	// Check if there's room to stand on the ledge after pull-up
	// ═══════════════════════════════════════════════════════════════

	FVector Phase5Origin = LedgeTopPoint + WallNormal * (CapsuleRadius + 10.f);
	float Phase5Distance = CapsuleHalfHeight * 2.f;
	FVector Phase5Dir = FVector::UpVector * Phase5Distance;

	HitResult = MakeShared<FHitResult>();
	Barrage->CastRay(
		Phase5Origin,
		Phase5Dir,
		BPFilter, LedgeFilter, BodyFilter,
		HitResult);

	bool bCanPullUp = !HitResult->bBlockingHit;

	// ═══════════════════════════════════════════════════════════════
	// RESULT
	// ═══════════════════════════════════════════════════════════════

	OutCandidate.LedgeTopPoint = LedgeTopPoint;
	OutCandidate.WallHitPoint = WallHitPoint;
	OutCandidate.WallNormal = WallNormal;
	OutCandidate.LedgeHeight = LedgeHeight;
	OutCandidate.bCanPullUp = bCanPullUp;
	OutCandidate.bValid = true;
}
