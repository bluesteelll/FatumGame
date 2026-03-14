// Weapon collision detection for AFlecsCharacter.
// 5 Barrage raycasts detect nearby static geometry:
//   3 horizontal (center + left/right spread) — detect walls, corners, doorframes
//   2 vertical (up/down spread) — detect ceilings vs floors to choose retraction direction
// Drives smooth weapon retraction to high-ready or low-ready pose.

#include "FlecsCharacter.h"
#include "FlecsWeaponProfile.h"
#include "BarrageDispatch.h"
#include "EPhysicsLayer.h"
#include "PhysicsFilters/FastObjectLayerFilters.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Controller.h"

void AFlecsCharacter::TickWeaponCollision(float DeltaTime)
{
	const UFlecsWeaponProfile* Profile = RecoilState.CachedProfile;
	if (!Profile || !FollowCamera) return;
	if (Profile->CollisionTraceDistance <= 0.f) return;

	UBarrageDispatch* Barrage = UBarrageDispatch::SelfPtr;
	if (!Barrage) return;

	// ── Raycast setup ──
	const FVector CameraPos = FollowCamera->GetComponentLocation();
	const FRotator AimRot = GetControlRotation();
	const FVector AimDir = AimRot.Vector();
	const FVector CamUp = FRotationMatrix(AimRot).GetScaledAxis(EAxis::Z);
	const FVector CamRight = FRotationMatrix(AimRot).GetScaledAxis(EAxis::Y);

	const float TraceDistance = Profile->CollisionTraceDistance;

	ensureMsgf(Profile->CollisionStartRetractDistance > Profile->CollisionFullRetractDistance,
		TEXT("WeaponProfile: CollisionStartRetractDistance (%.1f) must be > CollisionFullRetractDistance (%.1f)"),
		Profile->CollisionStartRetractDistance, Profile->CollisionFullRetractDistance);

	// Filters: CAST_QUERY_LEVEL_GEOMETRY_ONLY collides ONLY with NON_MOVING.
	// Character is on MOVING layer — no self-hit possible.
	auto BPFilter = Barrage->GetDefaultBroadPhaseLayerFilter(Layers::CAST_QUERY_LEVEL_GEOMETRY_ONLY);
	FastIncludeObjectLayerFilter ObjFilter(Layers::NON_MOVING);
	JPH::BodyFilter BodyFilter;

	// Reuse one FHitResult allocation across all casts.
	TSharedPtr<FHitResult> Hit = MakeShared<FHitResult>();
	float MinHitDistance = TraceDistance;
	float UpHitDistance = TraceDistance;
	float DownHitDistance = TraceDistance;

	auto CastRayAndGetDistance = [&](const FVector& Direction) -> float
	{
		*Hit = FHitResult();
		// CastRay direction param = direction * distance (NOT unit vector)
		Barrage->CastRay(CameraPos, Direction * TraceDistance, BPFilter, ObjFilter, BodyFilter, Hit);
		if (Hit->bBlockingHit)
			return Hit->Distance;
		return TraceDistance;
	};

	// ── 3 horizontal rays: center + left/right spread ──
	float CenterDist = CastRayAndGetDistance(AimDir);
	MinHitDistance = FMath::Min(MinHitDistance, CenterDist);

	if (Profile->CollisionRaySpreadAngle > 0.f)
	{
		FVector LeftDir = AimDir.RotateAngleAxis(-Profile->CollisionRaySpreadAngle, CamUp);
		FVector RightDir = AimDir.RotateAngleAxis(Profile->CollisionRaySpreadAngle, CamUp);
		LeftDir.Normalize();
		RightDir.Normalize();

		MinHitDistance = FMath::Min(MinHitDistance, CastRayAndGetDistance(LeftDir));
		MinHitDistance = FMath::Min(MinHitDistance, CastRayAndGetDistance(RightDir));
	}

	// ── 2 vertical rays: up + down spread ──
	if (Profile->CollisionVerticalSpreadAngle > 0.f)
	{
		FVector UpDir = AimDir.RotateAngleAxis(-Profile->CollisionVerticalSpreadAngle, CamRight);
		FVector DownDir = AimDir.RotateAngleAxis(Profile->CollisionVerticalSpreadAngle, CamRight);
		UpDir.Normalize();
		DownDir.Normalize();

		UpHitDistance = CastRayAndGetDistance(UpDir);
		DownHitDistance = CastRayAndGetDistance(DownDir);

		MinHitDistance = FMath::Min(MinHitDistance, UpHitDistance);
		MinHitDistance = FMath::Min(MinHitDistance, DownHitDistance);
	}

	// ── Compute target alpha from closest hit ──
	const float StartDist = Profile->CollisionStartRetractDistance;
	const float FullDist = Profile->CollisionFullRetractDistance;

	float RawAlpha = 0.f;
	if (MinHitDistance < StartDist && StartDist > FullDist)
	{
		RawAlpha = FMath::Clamp(1.f - (MinHitDistance - FullDist) / (StartDist - FullDist), 0.f, 1.f);
		RawAlpha = FMath::Pow(RawAlpha, Profile->CollisionAlphaPower);
	}
	RecoilState.CollisionTargetAlpha = RawAlpha;

	// ── Direction blend: 0 = up pose (default/wall), 1 = down pose (ceiling) ──
	// Upper ray closer than lower → obstacle above → retract DOWN (blend → 1)
	// Lower ray closer or equal → obstacle below/wall → retract UP (blend → 0)
	float TargetDirection = 0.f;
	if (RawAlpha > 0.f && Profile->CollisionVerticalSpreadAngle > 0.f)
	{
		if (UpHitDistance < DownHitDistance)
		{
			// Obstacle above — blend toward down pose proportionally
			float VerticalRatio = 1.f - FMath::Clamp(UpHitDistance / FMath::Max(DownHitDistance, 1.f), 0.f, 1.f);
			TargetDirection = VerticalRatio;
		}
		// else: lower/equal → stays 0 (up pose)
	}
	RecoilState.CollisionDirectionBlend = TargetDirection;

	// ── Asymmetric interpolation: fast retract, slow restore ──
	float InterpSpeed = (RawAlpha > RecoilState.CollisionCurrentAlpha)
		? Profile->CollisionRetractSpeed
		: Profile->CollisionRestoreSpeed;

	RecoilState.CollisionCurrentAlpha = FMath::FInterpTo(
		RecoilState.CollisionCurrentAlpha, RawAlpha, DeltaTime, InterpSpeed);

	// Direction blend follows the same retract speed (direction change should be responsive)
	RecoilState.CollisionCurrentDirectionBlend = FMath::FInterpTo(
		RecoilState.CollisionCurrentDirectionBlend, TargetDirection, DeltaTime, Profile->CollisionRetractSpeed);

	// Snap to zero when very small
	if (RecoilState.CollisionCurrentAlpha < 0.001f)
		RecoilState.CollisionCurrentAlpha = 0.f;

	// ── Compute final offsets: lerp between up and down poses ──
	if (RecoilState.CollisionCurrentAlpha > 0.f)
	{
		float Alpha = RecoilState.CollisionCurrentAlpha;
		float DirBlend = RecoilState.CollisionCurrentDirectionBlend;

		// Blend position between up pose and down pose
		FVector PoseOffset = FMath::Lerp(Profile->CollisionReadyPoseOffset, Profile->CollisionReadyPoseOffsetDown, DirBlend);
		RecoilState.CollisionPositionOffset = PoseOffset * Alpha;

		// Blend rotation between up pose and down pose
		FQuat UpQuat = Profile->CollisionReadyPoseRotation.Quaternion();
		FQuat DownQuat = Profile->CollisionReadyPoseRotationDown.Quaternion();
		FQuat BlendedQuat = FQuat::Slerp(UpQuat, DownQuat, DirBlend);
		RecoilState.CollisionRotationOffset = BlendedQuat.Rotator() * Alpha;
	}
	else
	{
		RecoilState.CollisionPositionOffset = FVector::ZeroVector;
		RecoilState.CollisionRotationOffset = FRotator::ZeroRotator;
	}
}
