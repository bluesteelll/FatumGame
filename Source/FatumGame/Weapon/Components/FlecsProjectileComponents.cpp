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
	S.bMaintainSpeed = Profile->bMaintainSpeed;
	S.TargetSpeed = Profile->DefaultSpeed;
	return S;
}
