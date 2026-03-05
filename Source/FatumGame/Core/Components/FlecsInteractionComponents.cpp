// FromProfile() factory method for interaction static component.

#include "FlecsInteractionComponents.h"
#include "FlecsInteractionProfile.h"

FInteractionStatic FInteractionStatic::FromProfile(const UFlecsInteractionProfile* Profile)
{
	check(Profile);

	FInteractionStatic S;
	S.MaxRange = Profile->InteractionRange;
	S.bSingleUse = Profile->bSingleUse;
	S.InteractionType = static_cast<uint8>(Profile->InteractionType);
	S.InstantAction = static_cast<uint8>(Profile->InstantAction);
	S.bRestrictAngle = Profile->bRestrictAngle;
	if (Profile->bRestrictAngle)
	{
		S.AngleCosine = FMath::Cos(FMath::DegreesToRadians(Profile->InteractionAngle));
		FVector Dir = Profile->InteractionDirection.GetSafeNormal();
		S.AngleDirection = Dir.IsNearlyZero() ? FVector::ForwardVector : Dir;
	}
	return S;
}
