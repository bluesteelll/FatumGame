// Lock-free bridge for "latest-value-wins" data from game thread to sim thread.
// Uses TTripleBuffer<T> — zero locks, zero contention.
//
// Game thread: Write*() methods (lock-free, ~ns)
// Sim thread: ApplyAll() once per tick before ProgressWorld()
//
// NOT for discrete events (fire/reload/stop) — those use CommandQueue.

#pragma once

#include "CoreMinimal.h"
#include "Containers/TripleBuffer.h"
#include "FlecsWeaponComponents.h"
#include <atomic>

namespace flecs { struct world; }

class FLateSyncBridge
{
public:
	// ─── Game Thread Writers ───────────────────────────

	/** Write latest aim direction for a character entity. Lock-free, ~ns. */
	void WriteAimDirection(int64 CharacterEntityId, const FAimDirection& AimDir);

	// ─── Sim Thread Consumer ───────────────────────────

	/** Apply all dirty buffers to Flecs entities. Call once per sim tick before ProgressWorld(). */
	void ApplyAll(flecs::world* World);

	// ─── Diagnostics ──────────────────────────────────

	/** Write sequence counter (game thread increments) */
	std::atomic<uint64> WriteSeqNum{0};

	/** Read sequence counter (sim thread increments) */
	std::atomic<uint64> ReadSeqNum{0};

	/** Last written camera position (for inverted flow in ProcessPendingProjectileSpawns) */
	std::atomic<float> LastWrittenX{0.f};
	std::atomic<float> LastWrittenY{0.f};
	std::atomic<float> LastWrittenZ{0.f};

	/** Last written muzzle world position (follows weapon mesh socket animation) */
	std::atomic<float> LastWrittenMuzzleX{0.f};
	std::atomic<float> LastWrittenMuzzleY{0.f};
	std::atomic<float> LastWrittenMuzzleZ{0.f};

private:
	TTripleBuffer<FAimDirection> AimBuffer;
	std::atomic<int64> AimCharacterEntityId{0};
};
