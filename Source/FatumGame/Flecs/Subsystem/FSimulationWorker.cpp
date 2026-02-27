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
		DeltaTime = FMath::Clamp(DeltaTime, 0.0001f, 0.05f);

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
			FlecsSubsystem->PrepareCharacterStep(DeltaTime);
		}

		if (!bRunning.load(std::memory_order_acquire)) break;

		if (BarrageDispatch)
		{
			BarrageDispatch->StackUp();

			if (!bRunning.load(std::memory_order_acquire)) break;

			BarrageDispatch->StepWorld(DeltaTime, TickCount);

			// Read resolved character positions from Barrage → atomics (zero-copy bridge)
			if (FlecsSubsystem)
			{
				FlecsSubsystem->SyncCharacterPositions();
			}

			if (!bRunning.load(std::memory_order_acquire)) break;

			BarrageDispatch->BroadcastContactEvents();
		}

		if (FlecsSubsystem)
		{
			FlecsSubsystem->ApplyLateSyncBuffers();
			FlecsSubsystem->ProgressWorld(DeltaTime);
		}

		++TickCount;

		// Publish timing for game thread interpolation.
		// Order matters: SimTickCount is the "version guard" — store it LAST
		// so game thread sees consistent DeltaTime + TimeSeconds when it reads the new tick count.
		LastSimDeltaTime.store(DeltaTime, std::memory_order_release);
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
