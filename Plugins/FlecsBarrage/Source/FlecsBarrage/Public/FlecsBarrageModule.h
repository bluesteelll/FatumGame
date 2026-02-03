// FlecsBarrage Module - Bridge between Flecs ECS and Barrage Physics

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FFlecsBarrageModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
