// FlecsUI Module - General UI infrastructure for Flecs simulation

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FFlecsUIModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
