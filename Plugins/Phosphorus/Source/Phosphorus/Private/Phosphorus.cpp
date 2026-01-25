// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "Phosphorus.h"

// Define log category
DEFINE_LOG_CATEGORY(LogPhosphorus);

#define LOCTEXT_NAMESPACE "FPhosphorus"

void FPhosphorusModule::StartupModule()
{
	UE_LOG(LogPhosphorus, Log, TEXT("Phosphorus module started"));
	UE_LOG(LogPhosphorus, Log, TEXT("  - Max types per dispatcher: %d"), Phosphorus::MaxTypeIndex);
	UE_LOG(LogPhosphorus, Log, TEXT("  - Template-based dispatcher: TPhosphorusDispatcher<TPayload>"));
}

void FPhosphorusModule::ShutdownModule()
{
	UE_LOG(LogPhosphorus, Log, TEXT("Phosphorus module shutdown"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPhosphorusModule, Phosphorus)
