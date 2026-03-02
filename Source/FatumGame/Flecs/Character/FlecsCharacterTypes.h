// Shared types for character ↔ simulation thread communication.
// Kept in a separate header to avoid pulling FlecsCharacter.h into subsystem code.

#pragma once

#include <atomic>

/** Game→sim input atomics. Written by AFlecsCharacter input handlers every tick (latest-wins).
 *  Read by PrepareCharacterStep on sim thread. Lock-free, no queue buildup. */
struct FCharacterInputAtomics
{
	// Movement direction (world-space, written by Move())
	std::atomic<float> DirX{0.f};
	std::atomic<float> DirZ{0.f};

	// Camera position/direction (written every Tick for blink targeting + mantle hang exit)
	std::atomic<float> CamLocX{0.f}, CamLocY{0.f}, CamLocZ{0.f};
	std::atomic<float> CamDirX{0.f}, CamDirY{0.f}, CamDirZ{0.f};

	// Button state (game thread writes, sim thread reads)
	std::atomic<bool> bBlinkHeld{false};
	std::atomic<bool> bCrouchHeld{false};
	mutable std::atomic<bool> bJumpPressed{false};   // one-shot, consumed (set false) by sim thread
	std::atomic<bool> bSprinting{false};
};
