// FromProfile() factory methods for penetration components.

#include "FlecsPenetrationComponents.h"
#include "FlecsProjectileProfile.h"
#include "FlecsPhysicsProfile.h"

FPenetrationStatic FPenetrationStatic::FromProfile(const UFlecsProjectileProfile* Profile)
{
	check(Profile);

	FPenetrationStatic S;
	S.PenetrationBudget = Profile->PenetrationBudget;
	S.MaxPenetrations = Profile->MaxPenetrations;
	S.DamageFalloffFactor = Profile->PenetrationDamageFalloff;
	S.VelocityFalloffFactor = Profile->PenetrationVelocityFalloff;
	S.RicochetCosAngleThreshold = FMath::Cos(FMath::DegreesToRadians(Profile->PenetrationRicochetAngleDeg));
	S.ImpulseTransferFactor = Profile->PenetrationImpulseTransfer;
	return S;
}

FPenetrationMaterial FPenetrationMaterial::FromProfile(const UFlecsPhysicsProfile* Profile)
{
	check(Profile);

	FPenetrationMaterial M;
	M.MaterialResistance = Profile->MaterialResistance;
	M.RicochetCosAngleThreshold = FMath::Cos(FMath::DegreesToRadians(Profile->PenetrationRicochetAngleDeg));
	return M;
}
