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

	// ─── Sim timing (published by sim thread, consumed by game thread for interpolation) ───
	std::atomic<uint64> SimTickCount{0};
	std::atomic<double> LastSimTickTimeSeconds{0.0};
	std::atomic<float>  LastSimDeltaTime{1.0f / 60.0f};

	// ─── Time dilation (written by game thread, read by sim thread) ───
	std::atomic<float> DesiredTimeScale{1.0f};
	std::atomic<bool>  bPlayerFullSpeed{true};
	std::atomic<float> TransitionSpeed{15.0f};

	// ─── Time dilation (written by sim thread, read by game thread for UE GlobalTimeDilation) ───
	std::atomic<float> ActiveTimeScalePublished{1.0f};

private:
	std::atomic<bool> bRunning{false};
	float ActiveTimeScale = 1.0f; // sim-thread-only, smoothed toward DesiredTimeScale
};
