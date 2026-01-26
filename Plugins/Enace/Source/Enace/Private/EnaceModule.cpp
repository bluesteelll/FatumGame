// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "EnaceModule.h"

DEFINE_LOG_CATEGORY(LogEnace);

#define LOCTEXT_NAMESPACE "FEnaceModule"

void FEnaceModule::StartupModule()
{
	UE_LOG(LogEnace, Log, TEXT("Enace module started"));
}

void FEnaceModule::ShutdownModule()
{
	UE_LOG(LogEnace, Log, TEXT("Enace module shutdown"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FEnaceModule, Enace)
