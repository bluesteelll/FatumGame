// UFlecsCraftingUISubsystem — owns per-station snapshot shared state (MJ3).

#include "FlecsCraftingUISubsystem.h"
#include "FlecsCraftingLog.h"

// ═══════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════

void UFlecsCraftingUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SelfPtr = this;
}

void UFlecsCraftingUISubsystem::Deinitialize()
{
	SelfPtr = nullptr;
	StationSharedStates.Empty();
	Super::Deinitialize();
}

bool UFlecsCraftingUISubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UFlecsCraftingUISubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFlecsCraftingUISubsystem, STATGROUP_Tickables);
}

// ═══════════════════════════════════════════════════════════════
// SHARED STATE MANAGEMENT
// ═══════════════════════════════════════════════════════════════

void UFlecsCraftingUISubsystem::CreateSharedState(FSkeletonKey StationKey)
{
	check(IsInGameThread());
	check(StationKey.IsValid());

	if (StationSharedStates.Contains(StationKey))
	{
		// Double-create: silent no-op. Can happen if sim thread issues redundant AsyncTasks
		// (e.g. re-spawn within same station key due to spawner retry); not an error.
		return;
	}

	StationSharedStates.Add(StationKey, MakeUnique<FCraftingStationSharedState>());

	UE_LOG(LogCrafting, Log, TEXT("CraftingUISubsystem: CreateSharedState key=0x%llX (total=%d)"),
		static_cast<unsigned long long>(StationKey.Obj), StationSharedStates.Num());
}

void UFlecsCraftingUISubsystem::DestroySharedState(FSkeletonKey StationKey)
{
	check(IsInGameThread());
	check(StationKey.IsValid());

	const int32 Removed = StationSharedStates.Remove(StationKey);

	UE_LOG(LogCrafting, Verbose, TEXT("CraftingUISubsystem: DestroySharedState key=0x%llX removed=%d"),
		static_cast<unsigned long long>(StationKey.Obj), Removed);
}

FCraftingStationSharedState* UFlecsCraftingUISubsystem::FindSharedState(FSkeletonKey StationKey)
{
	// Callable from sim thread (publisher) AND game thread (widget consumer).
	// Mutations (Create/Destroy) are game-thread-exclusive and occur only at station
	// spawn/destroy — rare events at Phase-1 scale (≤20 stations). Race window between
	// sim-thread Find and game-thread Remove is documented in §Step 10 thread-safety note.
	if (!StationKey.IsValid()) return nullptr;

	TUniquePtr<FCraftingStationSharedState>* Found = StationSharedStates.Find(StationKey);
	return Found ? Found->Get() : nullptr;
}
