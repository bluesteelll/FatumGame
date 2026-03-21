// FromProfile conversions for vitals components.

#include "FlecsVitalsComponents.h"
#include "FlecsVitalsProfile.h"
#include "FlecsVitalsItemProfile.h"
#include "FlecsTemperatureZoneProfile.h"

FVitalsStatic FVitalsStatic::FromProfile(const UFlecsVitalsProfile* Profile)
{
	check(Profile);

	FVitalsStatic S;
	S.HungerDrainRate = Profile->HungerDrainPerSecond;
	S.ThirstDrainRate = Profile->ThirstDrainPerSecond;
	S.WarmthTransitionSpeed = Profile->WarmthTransitionSpeed;
	S.StartingHunger = Profile->StartingHunger;
	S.StartingThirst = Profile->StartingThirst;
	S.StartingWarmth = Profile->StartingWarmth;

	// Copy thresholds from profile TArrays into fixed arrays (up to 4 entries)
	auto CopyThresholds = [](const TArray<FVitalThresholdConfig>& Src, FVitalThreshold Dst[4])
	{
		const int32 Count = FMath::Min(Src.Num(), 4);
		for (int32 i = 0; i < Count; ++i)
		{
			Dst[i].Percent = Src[i].ThresholdPercent;
			Dst[i].SpeedMultiplier = Src[i].SpeedMultiplier;
			Dst[i].HPDrainPerSecond = Src[i].HPDrainPerSecond;
			Dst[i].bCanSprint = Src[i].bCanSprint;
			Dst[i].bCanJump = Src[i].bCanJump;
			Dst[i].CrossVitalDrainMultiplier = Src[i].CrossVitalDrainMultiplier;
		}
	};

	CopyThresholds(Profile->HungerThresholds, S.HungerThresholds);
	CopyThresholds(Profile->ThirstThresholds, S.ThirstThresholds);
	CopyThresholds(Profile->WarmthThresholds, S.WarmthThresholds);

	return S;
}

FVitalsItemStatic FVitalsItemStatic::FromProfile(const UFlecsVitalsItemProfile* Profile)
{
	check(Profile);

	FVitalsItemStatic S;
	S.HungerRestore = Profile->HungerRestore;
	S.ThirstRestore = Profile->ThirstRestore;
	S.WarmthRestore = Profile->WarmthRestore;
	S.PassiveWarmthBonus = Profile->PassiveWarmthBonus;
	S.PassiveHungerDrainMult = Profile->PassiveHungerDrainMultiplier;
	S.PassiveThirstDrainMult = Profile->PassiveThirstDrainMultiplier;

	return S;
}

FTemperatureZoneStatic FTemperatureZoneStatic::FromProfile(const UFlecsTemperatureZoneProfile* Profile)
{
	check(Profile);

	FTemperatureZoneStatic S;
	S.Extent = Profile->Extent;
	S.Temperature = Profile->Temperature;

	return S;
}
