#pragma once

#include <cstdlib>
#include <cstdint>
#include <stdexcept>
#include <cstddef>
#include <cassert>
#include <utility>
#include <new>
#include "hedley.h"

namespace sse {

#ifndef likely
#define likely(x) HEDLEY_LIKELY(x)
#endif

// From http://stackoverflow.com/questions/12942548/making-stdvector-allocate-aligned-memory
// Accessed 11/7/16
enum class Alignment : size_t
{
    Normal = sizeof(void*),
    SSE    = 16,
    AVX    = 32,
    KB     = 64,
    KL     = 64,
    AVX512 = 64
};


#ifndef USE_ALIGNED_ALLOC
#  if (__cplusplus >= 201703L && defined(_GLIBCXX_HAVE_ALIGNED_ALLOC))
#    define USE_ALIGNED_ALLOC 1
#  else
#    define USE_ALIGNED_ALLOC 0
#  endif
#endif

} // namespace sse

