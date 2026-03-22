// UFlecsAmmoTypeDefinition implementation.

#include "FlecsAmmoTypeDefinition.h"
#include "FatumGameSettings.h"
#include "FlecsCaliberRegistry.h"

TArray<FName> UFlecsAmmoTypeDefinition::GetCaliberOptions() const
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
