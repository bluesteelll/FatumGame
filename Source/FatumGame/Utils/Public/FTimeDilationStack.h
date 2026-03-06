// Lightweight time dilation priority stack.
// Game-thread-only. Resolves multiple dilation sources via min-wins.

#pragma once

#include "CoreMinimal.h"

struct FDilationEntry
{
	FName Tag;                     // "BlinkAim", "FreezeFrame", "DeathCam"
	float DesiredScale = 1.f;      // [0.02, 1.0]
	float Duration = 0.f;          // 0 = indefinite (manual Remove)
	float Elapsed = 0.f;
	float EntrySpeed = 15.f;       // FInterpTo speed when entering
	float ExitSpeed = 10.f;        // FInterpTo speed when exiting
	bool bPlayerFullSpeed = true;  // player exempted from world dilation?
};

struct FTimeDilationStack
{
	// Add or replace an entry by Tag. If Tag already exists, overwrites it.
	void Push(const FDilationEntry& Entry)
	{
		for (auto& E : Entries)
		{
			if (E.Tag == Entry.Tag)
			{
				E = Entry;
				return;
			}
		}
		Entries.Add(Entry);
	}

	// Remove an entry by Tag. Auto-captures ExitSpeed for smooth transition back to 1.0.
	void Remove(FName Tag)
	{
		for (int32 i = Entries.Num() - 1; i >= 0; --i)
		{
			if (Entries[i].Tag == Tag)
			{
				LastExitSpeed = Entries[i].ExitSpeed;
				Entries.RemoveAtSwap(i);
			}
		}
	}

	// Tick all entries: advance elapsed, remove expired. Use REAL (undilated) DeltaTime.
	void Tick(float RealDeltaTime)
	{
		for (int32 i = Entries.Num() - 1; i >= 0; --i)
		{
			auto& E = Entries[i];
			if (E.Duration > 0.f)
			{
				E.Elapsed += RealDeltaTime;
				if (E.Elapsed >= E.Duration)
				{
					LastExitSpeed = E.ExitSpeed;
					Entries.RemoveAtSwap(i);
				}
			}
		}
	}

	// Min-wins: lowest DesiredScale among active entries. 1.0 if empty.
	float GetTargetScale() const
	{
		float MinScale = 1.f;
		for (const auto& E : Entries)
		{
			if (E.DesiredScale < MinScale) MinScale = E.DesiredScale;
		}
		return MinScale;
	}

	// bPlayerFullSpeed of the winning (lowest scale) entry.
	bool IsPlayerFullSpeed() const
	{
		float MinScale = 1.f;
		bool bResult = true; // default: no dilation active → player at full speed
		for (const auto& E : Entries)
		{
			if (E.DesiredScale < MinScale)
			{
				MinScale = E.DesiredScale;
				bResult = E.bPlayerFullSpeed;
			}
		}
		return bResult;
	}

	// Transition speed of the winning entry (for sim-thread smoothing).
	float GetTransitionSpeed() const
	{
		float MinScale = 1.f;
		float Speed = 10.f;
		for (const auto& E : Entries)
		{
			if (E.DesiredScale < MinScale)
			{
				MinScale = E.DesiredScale;
				Speed = E.EntrySpeed;
			}
		}
		// If stack is empty (returning to 1.0), use last known exit speed
		if (Entries.Num() == 0) return LastExitSpeed;
		return Speed;
	}

	bool IsActive() const { return Entries.Num() > 0; }

	TArray<FDilationEntry, TInlineAllocator<4>> Entries;
	float LastExitSpeed = 10.f; // cached for smooth exit when stack empties
};
