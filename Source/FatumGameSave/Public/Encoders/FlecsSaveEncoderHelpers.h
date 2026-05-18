// FlecsSaveEncoderHelpers — shared inline helpers used by every per-domain encoder cpp.
//
// CRITICAL: Each encoder cpp historically defined its own SaveValue template inside an
// anonymous namespace. That works in non-unity builds (each cpp is its own TU) but
// breaks when UBT merges multiple encoder cpps into a unity TU — anonymous namespaces
// in the same TU collapse into one, so 11 SaveValue definitions become 11 redefinitions
// in one anonymous namespace and the compiler rejects them.
//
// Fix: pull the helper into a single inline-templated header inside a named namespace.
// Each encoder cpp pulls it in via `#include` and adds a `using FlecsSaveEnc::SaveValue;`
// after the includes. Multiple TUs each see their own inline template instantiation;
// the linker dedupes via the template's vague linkage. Inside the unity TU only ONE
// definition exists (the header is #pragma once).

#pragma once

#include "CoreMinimal.h"
#include "Serialization/Archive.h"

namespace FlecsSaveEnc
{
	/** Serialize a value through an FArchive without const_cast UB. FArchive::operator<<
	 *  takes a non-const ref (UObject reflection paths may mutate during serialize); for
	 *  POD writes we take a local copy so the caller may pass `constexpr X` or a temporary. */
	template <typename T>
	FORCEINLINE void SaveValue(FArchive& Ar, T Value)
	{
		T Tmp = Value;
		Ar << Tmp;
	}
}
