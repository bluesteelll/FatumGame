// Simulation thread: drives Barrage physics + Flecs ECS progress.

#include "FSimulationWorker.h"
#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

bool FSimulationWorker::Init()
{
	bRunning.store(true, std::memory_order_release);
	UE_LOG(LogTemp, Display, TEXT("SimulationWorker: Init"));
	return true;
}

uint32 FSimulationWorker::Run()
{
	UE_LOG(LogTemp, Warning, TEXT("SimulationWorker: Run() started"));

	// Register this thread with Barrage for physics read/write access
	if (BarrageDispatch)
	{
		BarrageDispatch->GrantClientFeed();
	}

	// Target ~60 Hz sim rate (matches game thread update rate).
	// Higher rates cause physics body drift between SetPosition updates.
	constexpr float TargetTickSeconds = 1.0f / 60.0f;

	auto LastTime = std::chrono::high_resolution_clock::now();
	uint64_t TickCount = 0;

	while (bRunning.load(std::memory_order_acquire))
	{
		auto TickStart = std::chrono::high_resolution_clock::now();
		float DeltaTime = std::chrono::duration<float>(TickStart - LastTime).count();
		LastTime = TickStart;

		// Clamp delta to prevent spiral of death
		const float RealDT = FMath::Clamp(DeltaTime, 0.0001f, 0.05f);

		// ── Time dilation: smooth toward desired scale ──
		{
			float TargetScale = FMath::Clamp(
				DesiredTimeScale.load(std::memory_order_relaxed), 0.02f, 1.0f);
			float InterpSpeed = TransitionSpeed.load(std::memory_order_relaxed);
			ActiveTimeScale = FMath::FInterpTo(ActiveTimeScale, TargetScale, RealDT, InterpSpeed);
			if (FMath::Abs(ActiveTimeScale - TargetScale) < 0.001f)
				ActiveTimeScale = TargetScale;
		}
		const float DilatedDT = RealDT * ActiveTimeScale;
		const bool bPlayerFull = bPlayerFullSpeed.load(std::memory_order_relaxed);

		// Publish smoothed scale for game thread (UE GlobalTimeDilation mirrors this)
		ActiveTimeScalePublished.store(ActiveTimeScale, std::memory_order_relaxed);

		// Drain game thread commands (lock-free MPSC queue)
		if (FlecsSubsystem)
		{
			FlecsSubsystem->DrainCommandQueue();
		}

		if (!bRunning.load(std::memory_order_acquire)) break;

		// Compute acceleration-smoothed locomotion for characters.
		// Must run AFTER DrainCommandQueue (state flags set there) and BEFORE StackUp
		// (which processes mLocomotionUpdate via IngestUpdate).
		if (FlecsSubsystem)
		{
			FlecsSubsystem->PrepareCharacterStep(RealDT, DilatedDT, ActiveTimeScale, bPlayerFull);
		}

		if (!bRunning.load(std::memory_order_acquire)) break;

		if (BarrageDispatch)
		{
			BarrageDispatch->StackUp();

			if (!bRunning.load(std::memory_order_acquire)) break;

			BarrageDispatch->StepWorld(DilatedDT, TickCount);

			if (!bRunning.load(std::memory_order_acquire)) break;

			BarrageDispatch->BroadcastContactEvents();
		}

		if (FlecsSubsystem)
		{
			FlecsSubsystem->ApplyLateSyncBuffers();
			FlecsSubsystem->ProgressWorld(DilatedDT);
		}

		++TickCount;

		// Publish timing for game thread interpolation.
		// Use RealDT — ticks still happen at ~60Hz real, positions just change less per tick.
		// Order matters: SimTickCount is the "version guard" — store it LAST
		// so game thread sees consistent DeltaTime + TimeSeconds when it reads the new tick count.
		LastSimDeltaTime.store(RealDT, std::memory_order_release);
		LastSimTickTimeSeconds.store(FPlatformTime::Seconds(), std::memory_order_release);
		SimTickCount.store(TickCount, std::memory_order_release);

		// Rate limiter: sleep remaining time to hit ~60 Hz
		auto TickEnd = std::chrono::high_resolution_clock::now();
		float WorkTime = std::chrono::duration<float>(TickEnd - TickStart).count();
		float SleepTime = TargetTickSeconds - WorkTime;
		if (SleepTime > 0.0f)
		{
			FPlatformProcess::SleepNoStats(SleepTime);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("SimulationWorker: Run() ended"));
	return 0;
}

void FSimulationWorker::Stop()
{
	UE_LOG(LogTemp, Warning, TEXT("SimulationWorker: Stop() called"));
	bRunning.store(false, std::memory_order_release);
}
