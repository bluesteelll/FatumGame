// Copyright 2025 Oversized Sun Inc. All Rights Reserved.

#include "BarrageCollisionModule.h"

DEFINE_LOG_CATEGORY(LogBarrageCollision);

IMPLEMENT_MODULE(FBarrageCollisionModule, BarrageCollision)

void FBarrageCollisionModule::StartupModule()
{
	UE_LOG(LogBarrageCollision, Log, TEXT("BarrageCollision module started"));
}

void FBarrageCollisionModule::ShutdownModule()
{
	UE_LOG(LogBarrageCollision, Log, TEXT("BarrageCollision module shutdown"));
}
