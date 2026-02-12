// Core types for the FlecsUI plugin.
// FUIOpHandle, EUIOpResult, FOpResult, delegates.

#pragma once

#include "CoreMinimal.h"
#include "FlecsUITypes.generated.h"

/** Result of an async UI operation (confirmed by sim thread). */
UENUM(BlueprintType)
enum class EUIOpResult : uint8
{
	Success,
	Failed,
	Cancelled
};

/** Handle returned when starting an async UI operation. */
USTRUCT(BlueprintType)
struct FLECSUI_API FUIOpHandle
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 OpId = 0;

	bool IsValid() const { return OpId != 0; }
};

/** Op result delivered from sim thread via MPSC queue. */
struct FOpResult
{
	uint32 OpId = 0;
	EUIOpResult Result = EUIOpResult::Failed;
};

/** Pending operation tracked by the model until confirmed. */
struct FPendingOp
{
	uint32 OpId = 0;
	TFunction<void(EUIOpResult)> OnComplete;
};

/** Delegate for op completion callbacks. */
DECLARE_DELEGATE_OneParam(FOnUIOpComplete, EUIOpResult /*Result*/);
