// UFlecsMagazineProfile implementation.

#include "FlecsMagazineProfile.h"
#include "FatumGameSettings.h"
#include "FlecsCaliberRegistry.h"

TArray<FName> UFlecsMagazineProfile::GetCaliberOptions() const
{
	if (const UFatumGameSettings* Settings = UFatumGameSettings::Get())
	{
		if (UFlecsCaliberRegistry* Registry = Settings->GetCaliberRegistry())
		{
			return Registry->Calibers;
		}
	}
	return {};
}
