// UFlecsQuickLoadProfile implementation.

#include "FlecsQuickLoadProfile.h"
#include "FatumGameSettings.h"
#include "FlecsCaliberRegistry.h"

TArray<FName> UFlecsQuickLoadProfile::GetCaliberOptions() const
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
