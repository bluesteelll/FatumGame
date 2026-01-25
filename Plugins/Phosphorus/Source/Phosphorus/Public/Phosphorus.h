// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Phosphorus Event Dispatch Framework

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// Public includes
#include "PhosphorusTypes.h"
#include "PhosphorusDispatcher.h"

/**
 * FPhosphorusModule - Event dispatch framework module
 *
 * Phosphorus provides a generic, high-performance event dispatch system
 * with matrix-based O(1) handler lookup and tag hierarchy support.
 *
 * Features:
 * - Template-based dispatcher for any payload type
 * - Matrix[TypeA][TypeB] -> Handler lookup
 * - Parent tag fallback for type hierarchy
 * - Detailed logging for debugging
 * - Per-dispatcher type registry (independent systems)
 *
 * Usage in other modules:
 *   #include "PhosphorusDispatcher.h"
 *
 *   TPhosphorusDispatcher<FMyPayload> Dispatcher(TEXT("MySystem"));
 *   Dispatcher.RegisterType(TAG_EntityType);
 *   Dispatcher.RegisterHandler(TAG_TypeA, TAG_TypeB, MyHandler);
 *   Dispatcher.Dispatch(TypeA, TypeB, PayloadData);
 */
class FPhosphorusModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Get module instance */
	static FPhosphorusModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FPhosphorusModule>("Phosphorus");
	}

	/** Check if module is loaded */
	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("Phosphorus");
	}
};
