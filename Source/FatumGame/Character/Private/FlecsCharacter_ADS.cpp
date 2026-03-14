// ADS (Aim Down Sights) for AFlecsCharacter.
// Socket-based sight alignment, alpha interpolation, blocking conditions.

#include "FlecsCharacter.h"
#include "FlecsWeaponProfile.h"
#include "FatumMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"

// ═══════════════════════════════════════════════════════════════════════════
// INPUT HANDLERS
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::OnADSStarted(const FInputActionValue& Value)
{
	RecoilState.bWantsADS = true;
}

void AFlecsCharacter::OnADSCompleted(const FInputActionValue& Value)
{
	RecoilState.bWantsADS = false;
}

// ═══════════════════════════════════════════════════════════════════════════
// COMPUTE ADS TRANSFORM (called on weapon equip)
// Determines weapon local transform that centers the sight socket on screen.
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::ComputeADSTransform()
{
	const UFlecsWeaponProfile* Profile = RecoilState.CachedProfile;
	if (!Profile || !WeaponMeshComponent) return;

	RecoilState.bADSTransformValid = false;

	// Manual override takes priority
	if (!Profile->ADSPositionOverride.IsNearlyZero(0.01f) || !Profile->ADSRotationOverride.IsNearlyZero(0.01f))
	{
		FTransform ADSTransform = BaseWeaponTransform;
		ADSTransform.SetLocation(ADSTransform.GetLocation() + Profile->ADSPositionOverride);

		FQuat BaseQuat = ADSTransform.GetRotation();
		FQuat OverrideQuat = Profile->ADSRotationOverride.Quaternion();
		ADSTransform.SetRotation(BaseQuat * OverrideQuat);

		RecoilState.ADSWeaponTransform = ADSTransform;
		RecoilState.bADSTransformValid = true;
		return;
	}

	// Socket-based auto-alignment.
	// CONTRACT: SightAnchorSocket must have its forward axis (X) parallel to the barrel.
	// Only position is auto-corrected; rotation assumes the socket is forward-aligned.
	if (!WeaponMeshComponent->GetSkeletalMeshAsset()) return;
	if (!WeaponMeshComponent->DoesSocketExist(Profile->SightAnchorSocket)) return;

	// Get sight socket position in weapon-local (component) space
	FTransform SocketTransform = WeaponMeshComponent->GetSocketTransform(Profile->SightAnchorSocket, RTS_Component);
	FVector SightLocalPos = SocketTransform.GetLocation();

	// Transform sight position into camera-local space using hip pose
	FVector SightInHipSpace = BaseWeaponTransform.TransformPosition(SightLocalPos);

	// Target: same depth (X), centered on screen (Y=0, Z=0)
	FVector ScreenCenter = FVector(SightInHipSpace.X, 0.f, 0.f);
	FVector ADSDelta = ScreenCenter - SightInHipSpace;
	ADSDelta.X = 0.f; // preserve depth to avoid near-plane clipping

	FTransform ADSTransform = BaseWeaponTransform;
	ADSTransform.SetLocation(ADSTransform.GetLocation() + ADSDelta);

	RecoilState.ADSWeaponTransform = ADSTransform;
	RecoilState.bADSTransformValid = true;
}

// ═══════════════════════════════════════════════════════════════════════════
// TICK ADS (alpha interpolation + blocking conditions)
// ═══════════════════════════════════════════════════════════════════════════

void AFlecsCharacter::TickADS(float DeltaTime)
{
	const UFlecsWeaponProfile* Profile = RecoilState.CachedProfile;
	if (!Profile)
	{
		RecoilState.ADSAlpha = 0.f;
		return;
	}

	// ── Blocking conditions: force exit ADS ──
	bool bBlocked = false;

	// Wall collision blocks ADS
	if (Profile->CollisionFireBlockThreshold > 0.f &&
		RecoilState.CollisionCurrentAlpha >= Profile->CollisionFireBlockThreshold)
	{
		bBlocked = true;
	}

	// Sprint + ADS: cancel sprint when entering ADS
	if (RecoilState.bWantsADS && !bBlocked && Profile->bADSCancelsSprint)
	{
		if (FatumMovement && FatumMovement->IsSprinting())
		{
			FatumMovement->RequestSprint(false);
			if (InputAtomics) InputAtomics->Sprinting.Write(false);
		}
	}

	// ── Determine target ──
	float TargetAlpha = (RecoilState.bWantsADS && !bBlocked && RecoilState.bADSTransformValid) ? 1.f : 0.f;

	// ── Interpolate ──
	float InterpSpeed = (TargetAlpha > RecoilState.ADSAlpha)
		? Profile->ADSTransitionInSpeed
		: Profile->ADSTransitionOutSpeed;

	RecoilState.ADSAlpha = FMath::FInterpTo(RecoilState.ADSAlpha, TargetAlpha, DeltaTime, InterpSpeed);

	// Snap thresholds
	if (RecoilState.ADSAlpha > 0.995f) RecoilState.ADSAlpha = 1.f;
	if (RecoilState.ADSAlpha < 0.005f) RecoilState.ADSAlpha = 0.f;
}
