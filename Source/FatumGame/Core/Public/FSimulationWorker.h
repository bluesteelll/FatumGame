// Simple simulation thread: drives Barrage physics + Flecs ECS progress.
// ~50 lines of lock-free code driving physics + ECS.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Containers/Queue.h"
#include "Templates/Function.h"
#include <atomic>
#include <chrono>

class UBarrageDispatch;
class UFlecsArtillerySubsystem;

class FATUMGAME_API FSimulationWorker : public FRunnable
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

	// ═══════════════════════════════════════════════════════════════
	// SEQUENCE-COUNTER FENCE (save/load support — per v2 §5.1)
	// ═══════════════════════════════════════════════════════════════
	//
	// Replaces every sleep-based fence in save/load code. Pattern:
	//   const uint64 Seq = Worker->EnqueueSeqCommand([](){ /* sim-thread work */ });
	//   if (!Worker->WaitForSequence(Seq, 2.0)) { /* timeout */ }
	//
	// Legacy `UFlecsArtillerySubsystem::EnqueueCommand` continues to drain via its own
	// `CommandQueue`. Seq queue is independent; the two do not share ordering. Use seq
	// queue when you need a fence.

	/** Allocate the next sequence number. Monotonic; wraparound at 2^64 is irrelevant. */
	uint64 AllocateCommandSeq();

	/** Enqueue a command with an attached sequence number. Returns the seq.
	 *  Sim thread marks CompletedCommandSeq = max-seq-in-batch after the command executes. */
	uint64 EnqueueSeqCommand(TFunction<void()> Cmd);

	/** Block (with light-sleep poll) until CompletedCommandSeq >= TargetSeq.
	 *  Returns true on completion, false on timeout. */
	bool WaitForSequence(uint64 TargetSeq, double TimeoutSeconds = 2.0);

	/** Sim-thread accessor: drain the seq command queue and update CompletedCommandSeq.
	 *  Drains AFTER BroadcastContactEvents and BEFORE ApplyLateSyncBuffers/progress() per
	 *  v2 §M7 (quiescent: physics applied, Flecs not yet ticked, no deferred ops queued).
	 *  Public so the loop body can call it directly. */
	void DrainSeqCommandQueueOnSimThread();

	/** Lock-free read of the sim-thread running flag. Used by drain loops on the sim
	 *  thread to break out cleanly mid-drain when Stop() has been signalled. */
	bool IsRunning() const { return bRunning.load(std::memory_order_acquire); }

private:
	std::atomic<bool> bRunning{false};
	float ActiveTimeScale = 1.0f; // sim-thread-only, smoothed toward DesiredTimeScale

	// ─── Sequence-counter fence ───────────────────────────────────────
	// Game thread allocates seq numbers via fetch_add(NextCommandSeq).
	// Sim thread drains the queue, runs each command, then publishes the highest seq it
	// saw this batch into CompletedCommandSeq with release ordering. Game thread reads
	// CompletedCommandSeq with acquire ordering inside WaitForSequence — the acquire/release
	// pair guarantees the sim-thread side effects of the command are visible to the game
	// thread once WaitForSequence returns true.
	std::atomic<uint64> NextCommandSeq { 1 };
	std::atomic<uint64> CompletedCommandSeq { 0 };

	struct FSeqCommand
	{
		uint64 Seq;
		TFunction<void()> Cmd;
	};
	TQueue<FSeqCommand, EQueueMode::Mpsc> SeqCommandQueue;
};
