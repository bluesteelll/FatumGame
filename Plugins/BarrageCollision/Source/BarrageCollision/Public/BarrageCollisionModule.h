// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBarrageCollision, Log, All);

/**
 * FBarrageCollisionModule - Runtime module for Barrage collision processing
 *
 * Integrates:
 * - Phosphorus event dispatch framework
 * - Barrage physics collision events
 * - SkeletonKey entity identification
 * - Artillery tag system
 */
class FBarrageCollisionModule : public IModuleInterface
{
public:
	static inline FBarrageCollisionModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FBarrageCollisionModule>("BarrageCollision");
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("BarrageCollision");
	}

	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
