// FlecsSaveComponentRegistry — singleton impl. Phase 1: no registrations.

#include "FlecsSaveComponentRegistry.h"
#include "FlecsSaveLog.h"

FFlecsSaveComponentRegistry& FFlecsSaveComponentRegistry::Get()
{
	// Construct-on-first-use. Static initialization order is unspecified across translation
	// units, but every REGISTER_SAVE_* macro routes through Get(), so the registry exists
	// by the time the first registration runs.
	static FFlecsSaveComponentRegistry Instance;
	return Instance;
}

void FFlecsSaveComponentRegistry::Register(const FlecsSave::FComponentDesc& Desc)
{
	checkf(Desc.TypeId != 0, TEXT("FlecsSaveRegistry: TypeId 0 is reserved"));
	checkf(Desc.DebugName != nullptr, TEXT("FlecsSaveRegistry: DebugName must be set (TypeId=0x%04X)"), Desc.TypeId);

	if (const FlecsSave::FComponentDesc* Existing = DescsByTypeId.Find(Desc.TypeId))
	{
		// Hard-stop: two encoders racing for the same TypeId is a programming error that
		// silently corrupts save files. Fail-fast at boot to surface the conflict immediately.
		checkf(false,
			TEXT("FlecsSaveRegistry: duplicate TypeId 0x%04X — existing '%s', new '%s'"),
			Desc.TypeId, Existing->DebugName, Desc.DebugName);
		return;
	}

	if (!Desc.bIsTag)
	{
		checkf(Desc.Encoder != nullptr,
			TEXT("FlecsSaveRegistry: non-tag descriptor must provide Encoder (TypeId=0x%04X, '%s')"),
			Desc.TypeId, Desc.DebugName);
		checkf(Desc.Decoder != nullptr,
			TEXT("FlecsSaveRegistry: non-tag descriptor must provide Decoder (TypeId=0x%04X, '%s')"),
			Desc.TypeId, Desc.DebugName);
	}

	DescsByTypeId.Add(Desc.TypeId, Desc);
}

const FlecsSave::FComponentDesc* FFlecsSaveComponentRegistry::FindByTypeId(uint16 TypeId) const
{
	return DescsByTypeId.Find(TypeId);
}
