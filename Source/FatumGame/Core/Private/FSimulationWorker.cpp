// Simulation thread: drives Barrage physics + Flecs ECS progress.

#include "FSimulationWorker.h"
#include "FlecsArtillerySubsystem.h"
#include "BarrageDispatch.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Logging/LogMacros.h"

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

		// Drain game thread commands (lock-free MPSC queue) — legacy non-fenced API.
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

		if (!bRunning.load(std::memory_order_acquire)) break;

		// Drain the seq-fenced command queue at the v2 §M7 quiescent point:
		// physics has been stepped (StackUp + StepWorld + BroadcastContactEvents) so
		// Barrage body positions reflect the new state, but Flecs has NOT yet ticked
		// (ProgressWorld is below) so no observers / systems are running and no deferred
		// ops are queued. Any save snapshot walker enqueued here samples a coherent
		// pre-ECS-tick state — physics-resolved bodies paired with last-tick ECS data.
		DrainSeqCommandQueueOnSimThread();

		if (!bRunning.load(std::memory_order_acquire)) break;

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

// ═══════════════════════════════════════════════════════════════
// SEQUENCE-COUNTER FENCE (v2 §5.1)
// ═══════════════════════════════════════════════════════════════

uint64 FSimulationWorker::AllocateCommandSeq()
{
	// fetch_add returns the old value; we want a 1-based seq, so initial = 1.
	return NextCommandSeq.fetch_add(1, std::memory_order_relaxed);
}

uint64 FSimulationWorker::EnqueueSeqCommand(TFunction<void()> Cmd)
{
	const uint64 Seq = AllocateCommandSeq();
	SeqCommandQueue.Enqueue(FSeqCommand{ Seq, MoveTemp(Cmd) });
	return Seq;
}

void FSimulationWorker::DrainSeqCommandQueueOnSimThread()
{
	uint64 MaxSeqThisBatch = 0;

	FSeqCommand Entry;
	while (SeqCommandQueue.Dequeue(Entry))
	{
		// Stop check inside the drain loop: if Stop() was signalled mid-drain, abort
		// rather than execute lambdas against a teardown-state subsystem. Remaining
		// entries stay in the MPSC queue and are discarded when the worker tears down.
		if (!bRunning.load(std::memory_order_acquire))
		{
			break;
		}

		// Execute the queued work. The lambda may capture sim-thread-only state
		// (Worker* / Subsystem*) — that's the caller's responsibility.
		if (Entry.Cmd)
		{
			Entry.Cmd();
		}
		MaxSeqThisBatch = FMath::Max(MaxSeqThisBatch, Entry.Seq);
	}

	if (MaxSeqThisBatch != 0)
	{
		// Release-store so the game thread's acquire-load in WaitForSequence sees ALL the
		// data written by Entry.Cmd(). Single sync-edge replaces the old sleep-based fences.
		CompletedCommandSeq.store(MaxSeqThisBatch, std::memory_order_release);
	}
}

bool FSimulationWorker::WaitForSequence(uint64 TargetSeq, double TimeoutSeconds)
{
	const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;

	while (CompletedCommandSeq.load(std::memory_order_acquire) < TargetSeq)
	{
		if (FPlatformTime::Seconds() > Deadline)
		{
			UE_LOG(LogTemp, Error,
				TEXT("FSimulationWorker::WaitForSequence timeout — Target=%llu Completed=%llu"),
				TargetSeq,
				CompletedCommandSeq.load(std::memory_order_relaxed));
			return false;
		}
		// Light polling — 0.5 ms is well below a 60Hz frame (16.7ms) but enough to avoid
		// pegging the game thread CPU. The sleep ALSO yields to the sim thread on a 1-core
		// pinned scenario.
		FPlatformProcess::SleepNoStats(0.0005f);
	}
	return true;
}
