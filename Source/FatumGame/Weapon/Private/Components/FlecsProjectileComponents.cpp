// FromProfile() factory method for projectile static component.

#include "FlecsProjectileComponents.h"
#include "FlecsProjectileProfile.h"
#include "Properties/FlecsComponentProperties.h"

REGISTER_FLECS_COMPONENT(FProjectileInstance);

FProjectileStatic FProjectileStatic::FromProfile(const UFlecsProjectileProfile* Profile)
{
	check(Profile);

	FProjectileStatic S;
	S.MaxLifetime = Profile->Lifetime;
	S.MaxBounces = Profile->MaxBounces;
	S.GracePeriodFrames = Profile->GetGraceFrames();
	S.MinVelocity = Profile->MinVelocity;
	S.FuseTime = Profile->FuseTime;
	S.bMaintainSpeed = Profile->bMaintainSpeed;
	S.TargetSpeed = Profile->DefaultSpeed;

	// Damage falloff: precompute InvFalloffRange (0 if End <= Start).
	S.DamageFalloffStart = Profile->DamageFalloffStart;
	S.MinDamageMultiplier = Profile->MinDamageMultiplier;
	const float FalloffSpan = Profile->DamageFalloffEnd - Profile->DamageFalloffStart;
	S.InvFalloffRange = (FalloffSpan > KINDA_SMALL_NUMBER) ? (1.f / FalloffSpan) : 0.f;

	return S;
}
