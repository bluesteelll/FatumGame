// Shared types for character ↔ simulation thread communication.
// Kept in a separate header to avoid pulling FlecsCharacter.h into subsystem code.

#pragma once

#include <atomic>

/** Game→sim input direction atomics. Written by AFlecsCharacter::Move every tick (latest-wins).
 *  Read by PrepareCharacterStep on sim thread. Lock-free, no queue buildup. */
struct FCharacterInputAtomics
{
	std::atomic<float> DirX{0.f};  // world-space horizontal input (UE X = forward)
	std::atomic<float> DirZ{0.f};  // world-space horizontal input (UE Y = right)
};
