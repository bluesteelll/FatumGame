// UFlecsStationDebugHoverWidget — floating debug readout of a crafting station's internal state.
//
// Reads FCraftingStationSharedState from UFlecsCraftingUISubsystem via FSkeletonKey lookup.
// Widget Tick: check SimVersion atomic; if advanced, SwapAndRead triple buffer, rebuild text.
// Lifecycle: SetTargetStation(key) binds to a station; SetTargetStation(FSkeletonKey::Invalid())
// unbinds. Intended to be parented to HUD overlay; visibility toggled by character's hover hook.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SkeletonTypes.h"
#include "Library/FlecsCraftingSnapshot.h"
#include "FlecsStationDebugHoverWidget.generated.h"

class UTextBlock;
class UFlecsCraftingUISubsystem;
struct FCraftingStationSharedState;

UCLASS(Abstract, BlueprintType, Blueprintable)
class FATUMGAME_API UFlecsStationDebugHoverWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Game thread. Binds the widget to a station's shared state; null/invalid unbinds. */
	UFUNCTION(BlueprintCallable, Category = "Crafting|Debug")
	void SetTargetStation(FSkeletonKey StationKey);

	/** Latest snapshot read by the widget — exposed to BP for custom binding. */
	UFUNCTION(BlueprintCallable, Category = "Crafting|Debug")
	const FCraftingStationSnapshot& GetSnapshot() const { return CachedSnapshot; }

	/** Returns true when the widget has a valid target bound. */
	UFUNCTION(BlueprintCallable, Category = "Crafting|Debug")
	bool HasActiveTarget() const { return CurrentTarget.IsValid(); }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Override in Blueprint to repaint when the snapshot updates. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Crafting|Debug")
	void OnSnapshotUpdated(const FCraftingStationSnapshot& NewSnapshot);

private:
	/** Currently targeted station — set via SetTargetStation. */
	UPROPERTY(Transient)
	FSkeletonKey CurrentTarget = FSkeletonKey::Invalid();

	/** Last sim version seen to detect publish events without over-polling. */
	uint32 LastSeenSimVersion = 0;

	/** Latest snapshot copy (rebuilt on each detected publish). */
	UPROPERTY(Transient)
	FCraftingStationSnapshot CachedSnapshot;

	/** Cached subsystem pointer; refreshed lazily in Tick. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UFlecsCraftingUISubsystem> CachedSubsystem;

	/** Cached shared state — valid only while CurrentTarget stays stable. */
	FCraftingStationSharedState* CachedSharedState = nullptr;

	/** Rebind CachedSharedState / CachedSubsystem on target change. */
	void RebindChannel();
};
