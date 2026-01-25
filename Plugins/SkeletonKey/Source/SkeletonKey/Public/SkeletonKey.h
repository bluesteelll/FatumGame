// Copyright Breach Dogs, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
// ReSharper disable once CppUnusedIncludeDirective
//this is very much not unused. while transform dispatch DOES take care of this, it's not
//good practice to rely on that. so we intentionally include it here.
#include "SkeletonTypes.h"
#include "Modules/ModuleManager.h"

class FSkeletonKeyModule: public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
