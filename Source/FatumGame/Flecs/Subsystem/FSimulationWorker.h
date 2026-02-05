// Simple simulation thread: drives Barrage physics + Flecs ECS progress.
// ~50 lines of lock-free code driving physics + ECS.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include <atomic>
#include <chrono>

class UBarrageDispatch;
class UFlecsArtillerySubsystem;

class FSimulationWorker : public FRunnable
{
public:
	UBarrageDispatch* BarrageDispatch = nullptr;
	UFlecsArtillerySubsystem* FlecsSubsystem = nullptr;

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	std::atomic<bool> bRunning{false};
};
