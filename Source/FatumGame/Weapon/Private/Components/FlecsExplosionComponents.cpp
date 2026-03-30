// FromProfile() factory method for explosion static component.

#include "FlecsExplosionComponents.h"
#include "FlecsExplosionProfile.h"

FExplosionStatic FExplosionStatic::FromProfile(const UFlecsExplosionProfile* Profile)
{
	check(Profile);

	FExplosionStatic S;
	S.Radius = Profile->Radius;
	S.BaseDamage = Profile->BaseDamage;
	S.ImpulseStrength = Profile->ImpulseStrength;
	S.DamageFalloff = Profile->DamageFalloff;
	S.ImpulseFalloff = Profile->ImpulseFalloff;
	S.VerticalBias = Profile->VerticalBias;
	S.EpicenterLift = Profile->EpicenterLift;
	S.bDamageOwner = Profile->bDamageOwner;
	S.ExplosionEffect = Profile->ExplosionEffect;
	S.ExplosionEffectScale = Profile->ExplosionEffectScale;
	S.DamageType = Profile->DamageType;
	return S;
}
