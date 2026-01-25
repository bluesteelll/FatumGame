#pragma once
#define __builtin_alloca malloc
#include "LCM_Config.h"
typedef  unsigned long long ssize_t;
#include "vec.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <vector>
#include "wy.h"
#include "macros.h"
#include <numeric>

#include "div.h"
#include "policy.h"
#include "integral.h"

#include "hedley.h"



#ifdef INCLUDE_CLHASH_H_
#  define ENABLE_CLHASH 1
#elif ENABLE_CLHASH
#  include "clhash.h"
#endif


// Versioning
#define sk__str__(x) #x
#define sk__xstr__(x) sk__str__(x)
#define SKETCH_SHIFT 8
#define SKETCH_MAJOR 0
#define SKETCH_MINOR 19
#define SKETCH_REVISION 1
#define SKETCH_VERSION_INTEGER ((((SKETCH_MAJOR << SKETCH_SHIFT) | SKETCH_MINOR) << SKETCH_SHIFT) | SKETCH_REVISION)
#define SKETCH_VERSION SKETCH_MAJOR.SKETCH_MINOR##SKETCH_REVISION
#define SKETCH_VERSION_STR sk__xstr__(SKETCH_MAJOR.SKETCH_MINOR.SKETCH_REVISION)




#ifndef DBSKETCH_WRITE_STRING_MACROS
#define DBSKETCH_WRITE_STRING_MACROS \
    ssize_t write(const std::string &path, int compression=6) const {return write(path.data(), compression);}\
    ssize_t write(const char *path, int compression=6) const {\
        std::string mode = compression ? std::string("wb") + std::to_string(compression): std::string("wT");\
        gzFile fp = gzopen(path, mode.data());\
        if(!fp) throw ZlibError(Z_ERRNO, std::string("[") + __PRETTY_FUNCTION__ + "] " + std::string("Could not open file at '") + path + "' for writing");\
        auto ret = write(fp);\
        gzclose(fp);\
        return ret;\
    }

#define DBSKETCH_READ_STRING_MACROS \
    ssize_t read(const std::string &path) {return read(path.data());}\
    ssize_t read(const char *path) {\
        gzFile fp = gzopen(path, "rb");\
        if(!fp) throw std::runtime_error(std::string("Could not open file at '") + path + "' for reading");\
        ssize_t ret = read(fp);\
        gzclose(fp);\
        return ret;\
    }
#endif

namespace sketch {
#if defined(_MSC_VER) && !defined(__clang__)
    //__m128i
    inline __m128i operator&(__m128i a, const __m128i& b) {
        return _mm_and_si128(a, b);
    }
    inline __m128i& operator&=(__m128i a, const __m128i& b) {
        a = _mm_and_si128(a, b);
        return a;
    }
    // Bitwise XOR operator for __m512i
    inline __m128i operator^(__m128i a, const __m128i& b) {
        return _mm_xor_si128(a, b);
    }
    inline __m128i& operator^=(__m128i& a, const __m128i& b) {
        a = _mm_xor_si128(a, b);
        return a;
    }
    inline __uint128_t operator|(uint64_t Lhs, const __uint128_t& RHS) { return {RHS.high(), RHS.low() | Lhs};};
    inline __m128i operator~(const __m128i& I){return ~I;};
    inline __m128i operator|(__m128i& a, const __m128i& b) {
        return _mm_or_si128(a, b);
    }
    inline __uint128_t operator|(uint64_t& a, const __uint128_t& b) {
        return {b.high(), a | b.low()};
    }
    inline __m128i& operator|=(__m128i& a, const __m128i& b) {
        a = _mm_or_si128(a, b);
        return a;
    }
    // Bitwise AND operator for __m512i
    inline __m256i operator&(__m256i a, const __m256i& b) {
        return _mm256_and_si256(a, b);
    }
    inline __m256i& operator&=(__m256i a, const __m256i& b) {
        a = _mm256_and_si256(a, b);
        return a;
    }
    // Bitwise XOR operator for __m512i
    inline __m256i operator^(__m256i a, const __m256i& b) {
        return _mm256_xor_si256(a, b);
    }
    inline __m256i& operator^=(__m256i& a, const __m256i& b) {
        a = _mm256_xor_si256(a, b);
        return a;
    }
    inline __m256i operator|(const __m256i& Lhs, const __m256i& RHS) {return _mm256_or_si256(Lhs, RHS);};
    inline __m256i operator~(const __m256i& I){return ~I;};
    inline __m256i operator|(__m256i& a, const __m256i& b) {
        return _mm256_or_si256(a, b);
    }
    inline __m256i& operator|=(__m256i& a, const __m256i& b) {
        a = _mm256_or_si256(a, b);
        return a;
    }
#endif
inline namespace common {
using namespace integral;

using DefaultRNGType =  wy::WyHash<uint64_t, 2>;

template<typename BloomType>
inline double jaccard_index(const BloomType &h1, const BloomType &h2) {
    return h1.jaccard_index(h2);
}
template<typename BloomType>
inline double jaccard_index(BloomType &h1, BloomType &h2) {
    return h1.jaccard_index(h2);
}

template<typename T>
decltype(auto) intersection_size(const T &x, const T &y) {
    return x.intersection_size(y);
}

using std::uint64_t;
using std::uint32_t;
using std::uint16_t;
using std::uint8_t;
using std::int64_t;
using std::int32_t;
using std::int16_t;
using std::int8_t;
using std::size_t;

static constexpr auto AllocatorAlignment = sse::Alignment::
#if HAS_AVX_512
AVX512
#elif __AVX2__
AVX
#elif __SSE2__
SSE
#else
Normal
#endif
     ; // TODO: extend for POWER9 ISA


template<typename ValueType>
using Allocator = LaughablyDangerousAlignedAllocator<ValueType, AllocatorAlignment>;
#ifdef NOT_THREADSAFE

#ifndef SKETCH_THREADSAFE
#define SKETCH_THREADSAFE 0
#endif
#else
#ifndef SKETCH_THREADSAFE
#define SKETCH_THREADSAFE 1
#endif
#endif


#if __cplusplus >= 202002L
using std::identity;
#else
struct identity
{
    template<typename T>
    constexpr decltype(auto) operator()(T &&t) const {
        return std::forward<T>(t);
    }
};
#endif

template<typename I1, typename I2, typename Func=identity>
double geomean(I1 beg, I2 end, const Func &func=Func()) {
   return std::exp(std::accumulate(beg, end, 0., [&func](auto x, auto y) {return x + std::log(func(y));}) / std::distance(beg, end));
}
template<typename I1, typename I2, typename Func=identity>
double geomean_invprod(I1 beg, I2 end, double num=1., const Func &func=Func()) {
   return std::exp(std::log(num) - std::accumulate(beg, end, 0., [&func](auto x, auto y) {return x + std::log(func(y));}) / std::distance(beg, end));
}
template<typename I1, typename I2, typename Func=identity>
double arithmean(I1 beg, I2 end, const Func &func=Func()) {
   return std::accumulate(beg, end, 0., [&func](auto x, auto y) {return x + func(y);}) / std::distance(beg, end);
}
template<typename I1, typename I2, typename Func=identity>
double arithmean_invsim(I1 beg, I2 end, double num=1., const Func &func=Func()) {
   return std::accumulate(beg, end, 0., [&func,num](auto x, auto y) {return x + num / func(y);}) / std::distance(beg, end);
}


namespace lut {
static const uint8_t nhashesper64bitword [] {
/*
# Auto-generated using:
print("    0xFFu, %s" % ", ".join(str(64 // x) for x in range(1, 63)))
    0xFFu, 64, 32, 21, 16, 12, 10, 9, 8, 7, 6, 5, 5, 4, 4, 4, 4, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1

*/
    0xFFu, 64, 32, 21, 16, 12, 10, 9, 8, 7, 6, 5, 5, 4, 4, 4, 4, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};
} // namespace lut



template<typename T> class TD;


namespace sort {
// insertion_sort from https://github.com/orlp/pdqsort
// Slightly modified stylistically.
template<class Iter, class Compare>
inline void insertion_sort(Iter begin, Iter end, Compare comp) {
    using T = typename std::iterator_traits<Iter>::value_type;

    for (Iter cur = begin + 1; cur < end; ++cur) {
        Iter sift = cur;
        Iter sift_1 = cur - 1;

        // Compare first so we can avoid 2 moves for an element already positioned correctly.
        if (comp(*sift, *sift_1)) {
            T tmp = std::move(*sift);

            do { *sift-- = std::move(*sift_1); }
            while (sift != begin && comp(tmp, *--sift_1));

            *sift = std::move(tmp);
        }
    }
}
template<class Iter>
inline void insertion_sort(Iter begin, Iter end) {
    insertion_sort(begin, end, std::less<std::decay_t<decltype(*begin)>>());
}
} // namespace sort


struct DoNothing {
    template<typename... Args>void operator()(const Args &&...)const{}
    template<typename T>void operator()(const T &)const{}
};

namespace detail {
// Overloads for setting memory to 0 for either compact vectors
// or std::vectors
template<typename T, typename AllocatorType=typename T::allocator>
static inline void zero_memory(std::vector<T, AllocatorType> &v, size_t newsz) {
    std::memset(v.data(), 0, v.size() * sizeof(v[0]));
    v.resize(newsz);
}

template<typename T, size_t BUFFER_SIZE=64>
struct tmpbuffer {
    T *const ptr_;
    const size_t n_;
    T buf_[BUFFER_SIZE];
    tmpbuffer(size_t n):
        ptr_(n <= BUFFER_SIZE ? buf_: static_cast<T *>(malloc(n * sizeof(T)))),
        n_(n)
    {}
    tmpbuffer(const tmpbuffer &o) = delete;
    tmpbuffer(tmpbuffer &&o) = delete;
    tmpbuffer& operator=(const tmpbuffer &o) = delete;
    tmpbuffer& operator=(tmpbuffer &&o) = delete;
    size_t size() const {return n_;}
    T *get() {
        return ptr_;
    }
    const T *get() const {
        return ptr_;
    }
    ~tmpbuffer() {if(n_ > BUFFER_SIZE) std::free(ptr_);}

    auto &operator[](size_t i) {return ptr_[i];}
    auto operator[](size_t i) const {return ptr_[i];}
    // By value rather than reference

    auto begin() {return ptr_;}
    auto cbegin() const {return ptr_;}
    auto begin() const {return cbegin();}

    auto end() {return ptr_ + n_;}
    auto cend() const {return ptr_ + n_;}
    auto end()  const {return cend();}
};

} // namespace detail

template<typename T>
static constexpr int range_check(unsigned nbits, T val) {
    CONST_IF(std::is_floating_point<T>::value) {
        return 0; // Yeah, we're fine.
    }
    CONST_IF(std::is_signed<T>::value) {
        const int64_t v = val;
        return v < -int64_t(1ull << (nbits - 1)) ? -1
                                                 : v > int64_t((1ull << (nbits - 1)) - 1);
    } else {
        return val >= (1ull << nbits);
    }
}

enum MHCardinalityMode: uint8_t {
    HARMONIC_MEAN,
    GEOMETRIC_MEAN,
    ARITHMETIC_MEAN,
    MEDIAN,
    HLL_METHOD, // Should perform worse than harmonic
};

template<typename T, typename Alloc>
auto &delta_encode(std::vector<T, Alloc> &x) {
    assert(std::is_sorted(x.begin(), x.end()));
    T prev = 0;
    for(auto &el: x) {
        const auto tmp = el;
        el = tmp - prev;
        prev = tmp;
    }
    return x;
}

template<typename T, typename Alloc>
std::vector<T, Alloc> delta_encode(const std::vector<T, Alloc> &x) {
    std::vector<T, Alloc> ret(x);
    delta_encode(ret);
    return ret;
}

static inline double ab2cosine(double alpha, double beta) {
    return (1. - alpha - beta) / std::sqrt((1. - alpha) * (1. - beta));
}


#ifdef __CUDACC__
#endif

} // namespace common
} // namespace sketch
