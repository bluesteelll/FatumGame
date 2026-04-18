// UFlecsStationDebugHoverWidget implementation.

#include "FlecsStationDebugHoverWidget.h"
#include "FlecsCraftingUISubsystem.h"
#include "FlecsCraftingLog.h"

void UFlecsStationDebugHoverWidget::SetTargetStation(FSkeletonKey StationKey)
{
	if (StationKey == CurrentTarget)
	{
		return;
	}

	CurrentTarget = StationKey;
	LastSeenSimVersion = 0;
	CachedSharedState = nullptr;
	RebindChannel();
}

void UFlecsStationDebugHoverWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UWorld* W = GetWorld())
	{
		CachedSubsystem = W->GetSubsystem<UFlecsCraftingUISubsystem>();
	}
}

void UFlecsStationDebugHoverWidget::RebindChannel()
{
	CachedSharedState = nullptr;

	if (!CurrentTarget.IsValid())
	{
		return;
	}

	if (!CachedSubsystem.IsValid())
	{
		if (UWorld* W = GetWorld())
		{
			CachedSubsystem = W->GetSubsystem<UFlecsCraftingUISubsystem>();
		}
	}

	if (UFlecsCraftingUISubsystem* Sub = CachedSubsystem.Get())
	{
		CachedSharedState = Sub->FindSharedState(CurrentTarget);
	}
}

void UFlecsStationDebugHoverWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!CurrentTarget.IsValid())
	{
		return;
	}

	// Lazy rebind — shared state may not have existed at first SetTargetStation call
	// (AsyncTask ordering SJ1 — subsystem may register state 1 frame later).
	if (!CachedSharedState)
	{
		RebindChannel();
		if (!CachedSharedState)
		{
			return;
		}
	}

	const uint32 CurrentSimVersion = CachedSharedState->SimVersion.load(std::memory_order_acquire);
	if (CurrentSimVersion == LastSeenSimVersion)
	{
		return; // no publish since last tick
	}

	// New snapshot available — swap reader + consume.
	if (CachedSharedState->SnapshotBuffer.IsDirty())
	{
		CachedSharedState->SnapshotBuffer.SwapReadBuffers();
	}
	CachedSnapshot = CachedSharedState->SnapshotBuffer.Read();
	LastSeenSimVersion = CurrentSimVersion;

	OnSnapshotUpdated(CachedSnapshot);
}
