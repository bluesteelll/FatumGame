// Phase 5a — FConnectorSegmentStatic::FromDefinition implementation.

#include "Components/FlecsTransportComponents.h"
#include "FlecsCraftingLog.h"
#include "FlecsEntityDefinition.h"

FConnectorSegmentStatic FConnectorSegmentStatic::FromDefinition(const UFlecsEntityDefinition* Def)
{
	FConnectorSegmentStatic Out;
	if (!ensureMsgf(Def, TEXT("FConnectorSegmentStatic::FromDefinition called with null definition")))
	{
		return Out;
	}

	Out.Role            = Def->ConnectorTransportRole;
	Out.SegmentLengthCm = Def->ConnectorSegmentLengthCm;
	Out.FrontSocketName = Def->ConnectorFrontSocketName;
	Out.BackSocketName  = Def->ConnectorBackSocketName;

	if (Out.SegmentLengthCm <= 0.f)
	{
		UE_LOG(LogCrafting, Warning,
			TEXT("[Transport] FConnectorSegmentStatic: definition '%s' has SegmentLengthCm=%.2f — clamping to 100"),
			*Def->GetName(), Out.SegmentLengthCm);
		Out.SegmentLengthCm = 100.f;
	}

	return Out;
}
