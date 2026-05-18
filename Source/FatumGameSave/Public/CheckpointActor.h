// ACheckpointActor — drop-in trigger volume that calls UFlecsSaveSubsystem::RequestSave
// when the player overlaps. Phase 6 minimal implementation: auto-save on enter with a
// 1s cooldown to avoid spam when the player walks back and forth across the boundary.
//
// Lives in FatumGameSave so we keep the FatumGameSave -> FatumGame one-way dep — see
// the NOTE in FatumGame.Build.cs. Designers attach a visual mesh in the Blueprint child
// if desired; the C++ class only ships the trigger volume and the save call.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CheckpointActor.generated.h"

class USphereComponent;
class UPrimitiveComponent;
class AActor;

UCLASS(Blueprintable, BlueprintType, ClassGroup = (Save), meta = (DisplayName = "Checkpoint Actor"))
class FATUMGAMESAVE_API ACheckpointActor : public AActor
{
	GENERATED_BODY()

public:
	ACheckpointActor();

	// ═══════════════════════════════════════════════════════════════
	// CONFIG
	// ═══════════════════════════════════════════════════════════════

	/** Display name passed to UFlecsSaveSubsystem::RequestSave. Stored in the slot's header. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checkpoint")
	FName CheckpointName = TEXT("Checkpoint");

	/** Slot to write into. Defaults to LastSession (11). Use 10 for Quicksave or 0..9 for named. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checkpoint",
		meta = (ClampMin = "0", ClampMax = "11"))
	int32 TargetSaveSlot = 11;

	/** Trigger sphere radius (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checkpoint",
		meta = (ClampMin = "10", ClampMax = "5000"))
	float TriggerRadius = 200.f;

	/** When true, overlap by ANY pawn immediately fires a save (subject to cooldown).
	 *  When false, overlap arms the checkpoint but the save is not fired automatically —
	 *  another system would call FireSave() (e.g. on E press). Phase 6 leaves
	 *  the manual path unwired; designers can drive it from BP. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checkpoint")
	bool bAutoSaveOnEnter = true;

	/** Minimum seconds between consecutive auto-saves from the same checkpoint. Prevents
	 *  spam when the player walks across the boundary repeatedly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checkpoint",
		meta = (ClampMin = "0.1", ClampMax = "60.0"))
	float CooldownSeconds = 1.f;

	// ═══════════════════════════════════════════════════════════════
	// API (BP-callable, also invoked internally)
	// ═══════════════════════════════════════════════════════════════

	/** Trigger a save now. Honours cooldown. Returns true if the save was actually scheduled. */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	bool FireSave();

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	/** Sphere trigger volume — drives overlap callbacks. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Checkpoint|Components")
	TObjectPtr<USphereComponent> TriggerVolume;

private:
	/** World time (seconds) at which the next save is allowed. */
	double NextAllowedSaveTime = 0.0;
};
