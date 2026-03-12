// FromProfile conversions for stealth components.

#include "FlecsStealthComponents.h"

DEFINE_LOG_CATEGORY(LogStealth);
#include "FlecsStealthLightProfile.h"
#include "FlecsNoiseZoneProfile.h"

FStealthLightStatic FStealthLightStatic::FromProfile(const UFlecsStealthLightProfile* Profile)
{
	check(Profile);

	FStealthLightStatic S;
	S.Type = Profile->LightType;
	S.Intensity = Profile->Intensity;
	S.Radius = Profile->Radius;

	// Precompute cone cosines for spot lights (degrees -> cosine)
	S.InnerConeAngleCos = FMath::Cos(FMath::DegreesToRadians(Profile->InnerConeAngle));
	S.OuterConeAngleCos = FMath::Cos(FMath::DegreesToRadians(Profile->OuterConeAngle));

	// Direction is set per-entity at spawn time (from spawner rotation), not from profile
	S.Direction = FVector::ForwardVector;

	return S;
}

FNoiseZoneStatic FNoiseZoneStatic::FromProfile(const UFlecsNoiseZoneProfile* Profile)
{
	check(Profile);

	FNoiseZoneStatic S;
	S.Extent = Profile->Extent;
	S.SurfaceType = Profile->SurfaceType;

	return S;
}
