#pragma once
#include "CoreMinimal.h"
THIRD_PARTY_INCLUDES_START
#include "immintrin.h"

#include <memory>
#include "Memory/sseutil.h"
#include "Memory/aligned_allocator.h"
#define NO_BLAZE true
#define NOT_THREADSAFE true
#define NOSVML true
#define NO_SLEEF true
#define __SSE2__ true
#define __AVX2__ true
#define __AVX__ true
#define __SSE2__ true
#define LCM_USE_SSE4_2 true
//HI! you MAY want to change this. Might. Maybe. You know. If you value your life, hope, or sanity.

#if defined(_MSC_VER) && !defined(__clang__)
#include "Fallbacks/uint128.h"
using __uint128_t=Fortran::common::uint128_t;
#endif
//you could get this working...
//SKETCHALLOC sse::AlignedAllocator<ValueType, AllocatorAlignment>;

#define SKETCHALLOC AlignedAllocator<ValueType, AllocatorAlignment>
THIRD_PARTY_INCLUDES_END