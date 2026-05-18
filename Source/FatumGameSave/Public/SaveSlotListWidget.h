// USaveSlotListWidget — C++ data layer for save/load UI.
//
// Phase 6: minimal Blueprint-facing surface. Designers build the visual layout in
// a Blueprint child (WBP_SaveSlotList or similar) and call the BlueprintCallable
// API below to drive the underlying UFlecsSaveSubsystem. The widget also rebroadcasts
// OnSaveComplete / OnLoadComplete via dynamic delegates so BP can bind row-refresh
// logic without holding a direct subsystem reference.
//
// NO visual layout is created here — bring-your-own Blueprint widget.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlecsSaveTypes.h"
#include "SaveSlotListWidget.generated.h"

class UFlecsSaveSubsystem;

// Mirror the subsystem's delegates so BP can bind directly to the widget instead of
// fishing the subsystem out itself. Same signature as FOnSaveComplete / FOnLoadComplete.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnSaveSlotSaveComplete,
	int32, SlotIndex, FString, SlotName, ESaveResult, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnSaveSlotLoadComplete,
	int32, SlotIndex, FString, SlotName, ELoadResult, Result);

UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Save Slot List Widget"))
class FATUMGAMESAVE_API USaveSlotListWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════
	// QUERY
	// ═══════════════════════════════════════════════════════════════

	/** Number of slots managed by the subsystem (0..N-1). */
	UFUNCTION(BlueprintPure, Category = "Save UI")
	int32 GetSlotCount() const;

	/** Return one FSaveSlotInfo per slot — empty/unreadable slots are still returned with bExists=false.
	 *  Phase 6: header parse is deferred to a Phase 7 metadata pass; bExists + FileSizeBytes +
	 *  Timestamp are filled from the on-disk file. DisplayName / LevelName stay empty until the
	 *  metadata pass lands (we do not currently keep an out-of-band index file). */
	UFUNCTION(BlueprintCallable, Category = "Save UI")
	TArray<FSaveSlotInfo> GetAllSlotInfos() const;

	/** Look up a single slot. Same caveats as GetAllSlotInfos. */
	UFUNCTION(BlueprintCallable, Category = "Save UI")
	FSaveSlotInfo GetSlotInfo(int32 SlotIndex) const;

	// ═══════════════════════════════════════════════════════════════
	// COMMANDS (forward to the subsystem)
	// ═══════════════════════════════════════════════════════════════

	/** Enqueue a save into the given slot. Wraps UFlecsSaveSubsystem::RequestSave. */
	UFUNCTION(BlueprintCallable, Category = "Save UI")
	ESaveResult TriggerSave(int32 SlotIndex, const FString& DisplayName);

	/** Enqueue a load from the given slot. Wraps UFlecsSaveSubsystem::RequestLoad. */
	UFUNCTION(BlueprintCallable, Category = "Save UI")
	ELoadResult TriggerLoad(int32 SlotIndex);

	/** Delete .sav + .bak1..bak3 for the given slot. */
	UFUNCTION(BlueprintCallable, Category = "Save UI")
	bool DeleteSlot(int32 SlotIndex);

	/** True if a save is currently in flight. UI should disable Save/Load buttons. */
	UFUNCTION(BlueprintPure, Category = "Save UI")
	bool IsBusy() const;

	// ═══════════════════════════════════════════════════════════════
	// EVENTS (rebroadcast from the subsystem)
	// ═══════════════════════════════════════════════════════════════

	UPROPERTY(BlueprintAssignable, Category = "Save UI")
	FOnSaveSlotSaveComplete OnSaveComplete;

	UPROPERTY(BlueprintAssignable, Category = "Save UI")
	FOnSaveSlotLoadComplete OnLoadComplete;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Triple-guarded fetch of the subsystem (no world / no GI / not yet initialised). */
	UFlecsSaveSubsystem* GetSaveSubsystem() const;

private:
	UFUNCTION()
	void HandleSaveComplete(int32 SlotIndex, FString SlotName, ESaveResult Result);

	UFUNCTION()
	void HandleLoadComplete(int32 SlotIndex, FString SlotName, ELoadResult Result);

	/** Whether NativeConstruct managed to bind to the subsystem. NativeDestruct uses this
	 *  to know whether RemoveDynamic is safe (defensive — also bails if subsystem missing). */
	bool bSubscribed = false;
};
