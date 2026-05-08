// Phase 5a — designer-authored station port spec.
//
// One row per editor-defined transport port on a UFlecsEntityDefinition. Read by
// FlecsMultiblockRuntime::SetupStationInstance to populate the runtime FStationPorts
// component (which is plain-C++ and not directly editable in the editor).
//
// Cite: V2 PATCH 9 (final form). LocalOffsetCm is station-local; SocketName is debug-only.

#pragma once

#include "CoreMinimal.h"
#include "FlecsCraftingTypes.h"
#include "FlecsStationPortAuthoring.generated.h"

USTRUCT(BlueprintType)
struct FATUMGAME_API FStationPortAuthoring
{
	GENERATED_BODY()

	/** Direction + medium classification. None = inert (will not produce a runtime FPortSlot). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Port")
	EPortKind Kind = EPortKind::None;

	/** Optional debug label — runtime resolution uses LocalOffsetCm, NOT socket lookup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Port")
	FName SocketName;

	/** Station-local offset (cm) where the port socket lives. Rotated by station yaw at lookup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Port")
	FVector LocalOffsetCm = FVector::ZeroVector;

	/** Designer priority for inlet tie-break in 5c+ pour scheduling. Unused in 5a; passed through.
	 *  int32 here (not int8) because UHT BlueprintType doesn't support int8.
	 *  Clamped to int8 range [-128, 127] when copied into runtime FPortSlot::AcceptPriority (plain struct, no UHT). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Port", meta = (ClampMin = "-128", ClampMax = "127"))
	int32 AcceptPriority = 0;
};
