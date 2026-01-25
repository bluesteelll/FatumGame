// Copyright 2025 Oversized Sun Inc. All Rights Reserved.
// Phosphorus Event Dispatch Framework - Common Types

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

// ═══════════════════════════════════════════════════════════════════════════════
// LOGGING
// ═══════════════════════════════════════════════════════════════════════════════

PHOSPHORUS_API DECLARE_LOG_CATEGORY_EXTERN(LogPhosphorus, Log, All);

// Convenience macros for consistent logging format
#define PHOS_LOG(Verbosity, Dispatcher, Format, ...) \
	UE_LOG(LogPhosphorus, Verbosity, TEXT("[%s] " Format), *Dispatcher, ##__VA_ARGS__)

#define PHOS_LOG_FUNC(Verbosity, Dispatcher, Format, ...) \
	UE_LOG(LogPhosphorus, Verbosity, TEXT("[%s::%s] " Format), *Dispatcher, *FString(__FUNCTION__), ##__VA_ARGS__)

// ═══════════════════════════════════════════════════════════════════════════════
// CONSTANTS
// ═══════════════════════════════════════════════════════════════════════════════

namespace Phosphorus
{
	// Maximum number of types per dispatcher (uint8 index)
	constexpr uint8 MaxTypeIndex = 254;
	constexpr uint8 InvalidTypeIndex = 255;

	// Default dispatcher name for unnamed instances
	constexpr const TCHAR* DefaultDispatcherName = TEXT("Phosphorus");
}

// ═══════════════════════════════════════════════════════════════════════════════
// RESULT TYPES
// ═══════════════════════════════════════════════════════════════════════════════

namespace Phosphorus
{
	// Result of a dispatch operation
	enum class EDispatchResult : uint8
	{
		// No handler found for the type pair
		NoHandler,

		// Handler executed and returned false (continue processing)
		Handled,

		// Handler executed and returned true (stop processing)
		Consumed,

		// Dispatch skipped due to invalid input
		InvalidInput
	};

	// Result of a registration operation
	enum class ERegistrationResult : uint8
	{
		// Registration succeeded
		Success,

		// Registration succeeded, replaced existing handler
		Replaced,

		// Registration failed - invalid tag
		InvalidTag,

		// Registration failed - invalid handler
		InvalidHandler,

		// Registration failed - max types reached
		MaxTypesReached
	};

	// Convert results to string for logging
	inline const TCHAR* ToString(EDispatchResult Result)
	{
		switch (Result)
		{
			case EDispatchResult::NoHandler:    return TEXT("NoHandler");
			case EDispatchResult::Handled:      return TEXT("Handled");
			case EDispatchResult::Consumed:     return TEXT("Consumed");
			case EDispatchResult::InvalidInput: return TEXT("InvalidInput");
			default:                            return TEXT("Unknown");
		}
	}

	inline const TCHAR* ToString(ERegistrationResult Result)
	{
		switch (Result)
		{
			case ERegistrationResult::Success:         return TEXT("Success");
			case ERegistrationResult::Replaced:        return TEXT("Replaced");
			case ERegistrationResult::InvalidTag:      return TEXT("InvalidTag");
			case ERegistrationResult::InvalidHandler:  return TEXT("InvalidHandler");
			case ERegistrationResult::MaxTypesReached: return TEXT("MaxTypesReached");
			default:                                   return TEXT("Unknown");
		}
	}
}
