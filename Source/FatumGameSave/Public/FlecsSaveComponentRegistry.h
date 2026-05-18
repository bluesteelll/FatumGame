// FlecsSaveComponentRegistry — TypeId ↔ encoder/decoder lookup.
//
// PHASE 1: stub only. The registry compiles but contains zero entries. Real encoders
// are introduced in Phase 2+ via REGISTER_SAVE_COMPONENT/REGISTER_SAVE_TAG macros.
//
// TypeId allocation per v2 §M10:
//   0x0000–0x00FF — reserved / header
//   0x0100–0x0FFF — non-tag components (grouped by domain nibble)
//   0x1000–0x1FFF — zero-size tags
//   0x2000–0x2FFF — pairs
//   0xFFFF        — sentinel "end-of-list"
//
// Encoders/decoders are plain function pointers — registry lookup is a TMap on TypeId.
// Static-init registration is intentional: each domain file owns its own registrations
// and they self-assemble at module load time.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

namespace flecs { struct entity; }

namespace FlecsSave
{
	/** Encoder callback: read component from entity, append version-prefixed bytes to Out. */
	using FEncoderFn = void (*)(const flecs::entity& Entity, TArray<uint8>& OutBytes);

	/** Decoder callback: read bytes (version-prefixed), populate component on entity.
	 *  Returns false on unrecognized version or malformed payload. */
	using FDecoderFn = bool (*)(const flecs::entity& Entity, const uint8* Bytes, uint32 Len);

	/** Descriptor for one registered component or tag. */
	struct FATUMGAMESAVE_API FComponentDesc
	{
		/** Stable on-disk TypeId. See M10 ranges. */
		uint16 TypeId = 0;

		/** Latest version this encoder writes. Tags use 0. */
		uint16 Version = 0;

		/** Debug name (for logs); points to a string literal — never owned. */
		const TCHAR* DebugName = nullptr;

		/** Encoder/decoder; nullptr for tags (those use bIsTag=true and no payload). */
		FEncoderFn Encoder = nullptr;
		FDecoderFn Decoder = nullptr;

		/** True for zero-size tags. Tags carry no bytes — only the TypeId is written. */
		bool bIsTag = false;
	};
}

/** Singleton registry of all save-aware components and tags. */
class FATUMGAMESAVE_API FFlecsSaveComponentRegistry
{
public:
	/** Process-wide singleton. */
	static FFlecsSaveComponentRegistry& Get();

	/** Register a component or tag. Called from static initializers; safe to call once per TypeId.
	 *  Fail-fast on duplicate TypeId. */
	void Register(const FlecsSave::FComponentDesc& Desc);

	/** O(1) TypeId lookup. Returns nullptr for unknown ids (forward-compat skipping). */
	const FlecsSave::FComponentDesc* FindByTypeId(uint16 TypeId) const;

	/** O(1) name lookup. Used by the walker to dispatch on Flecs `entity.each(flecs::id)`
	 *  where the only available handle is the component's debug name string. Returns
	 *  nullptr for non-save-aware components (skip silently). */
	const FlecsSave::FComponentDesc* FindByDebugName(FName DebugName) const;

	/** Iterate every registered descriptor. Used by diagnostics and any future
	 *  registry-walk tooling. Order is unspecified. */
	template <typename Func>
	void ForEachDesc(Func&& Fn) const
	{
		for (const auto& Pair : DescsByTypeId)
		{
			Fn(Pair.Value);
		}
	}

	/** Total number of registered descriptors (components + tags). */
	int32 Num() const { return DescsByTypeId.Num(); }

private:
	FFlecsSaveComponentRegistry() = default;
	TMap<uint16, FlecsSave::FComponentDesc> DescsByTypeId;
	TMap<FName, uint16> DescsByName;
};

// ═══════════════════════════════════════════════════════════════
// REGISTRATION MACROS (Phase 2+ usage)
// ═══════════════════════════════════════════════════════════════
// PHASE 1 NOTE: defined here for completeness; no call sites yet.

#define REGISTER_SAVE_COMPONENT(TypeId_, ComponentType, Version_, EncoderFn, DecoderFn)        \
    static struct FAutoRegisterSaveComp_##ComponentType                                        \
    {                                                                                          \
        FAutoRegisterSaveComp_##ComponentType()                                                \
        {                                                                                      \
            FlecsSave::FComponentDesc D;                                                       \
            D.TypeId    = static_cast<uint16>(TypeId_);                                        \
            D.Version   = static_cast<uint16>(Version_);                                       \
            D.DebugName = TEXT(#ComponentType);                                                \
            D.Encoder   = (EncoderFn);                                                         \
            D.Decoder   = (DecoderFn);                                                         \
            D.bIsTag    = false;                                                               \
            FFlecsSaveComponentRegistry::Get().Register(D);                                    \
        }                                                                                      \
    } GAutoRegisterSaveComp_##ComponentType;

#define REGISTER_SAVE_TAG(TypeId_, TagType)                                                    \
    static struct FAutoRegisterSaveTag_##TagType                                               \
    {                                                                                          \
        FAutoRegisterSaveTag_##TagType()                                                       \
        {                                                                                      \
            FlecsSave::FComponentDesc D;                                                       \
            D.TypeId    = static_cast<uint16>(TypeId_);                                        \
            D.Version   = 0;                                                                   \
            D.DebugName = TEXT(#TagType);                                                      \
            D.Encoder   = nullptr;                                                             \
            D.Decoder   = nullptr;                                                             \
            D.bIsTag    = true;                                                                \
            FFlecsSaveComponentRegistry::Get().Register(D);                                    \
        }                                                                                      \
    } GAutoRegisterSaveTag_##TagType;
