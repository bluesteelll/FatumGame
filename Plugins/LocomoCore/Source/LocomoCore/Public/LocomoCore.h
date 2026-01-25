// Copyright Hedra Group.


#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
// ReSharper disable once CppUnusedIncludeDirective
THIRD_PARTY_INCLUDES_START
#include "Memory/IntraTickThreadblindAlloc.h"
#include "LocomoUtil.h"
THIRD_PARTY_INCLUDES_END

class FLocomoModuleAPI : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};