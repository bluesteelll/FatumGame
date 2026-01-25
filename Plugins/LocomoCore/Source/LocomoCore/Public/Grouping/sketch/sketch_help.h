#ifndef SKETCH_HELPERS_H___
#define SKETCH_HELPERS_H___
#include "hll.h"
#include "mh.h"
#include "bbmh.h"

namespace LCMS {
using namespace sketch;
using namespace hll;

enum Sketch {
    HLL,
    BLOOM_FILTER,
    RANGE_MINHASH,
    FULL_KHASH_SET,
    COUNTING_RANGE_MINHASH,
    HYPERMINHASH16,
    HYPERMINHASH32,
    TF_IDF_COUNTING_RANGE_MINHASH, // TODO. I'm willing to make two passes through the data for this.
    BB_MINHASH,
    COUNTING_BB_MINHASH, // TODO
};

static const char *sketch_names [] {
    "HLL",
    "BLOOM_FILTER",
    "RANGE_MINHASH",
    "FULL_KHASH_SET",
    "COUNTING_RANGE_MINHASH",
    "HYPERMINHASH16",
    "HYPERMINHASH32",
    "TF_IDF_COUNTING_RANGE_MINHASH",
    "BB_MINHASH",
    "COUNTING_BB_MINHASH",
};
using CRMFinal = mh::FinalCRMinHash<uint64_t, std::greater<uint64_t>>;
template<typename T>
double similarity(const T &a, const T &b) {
    return jaccard_index(a, b);
}
    
using CBBMinHashType = mh::CountingBBitMinHasher<uint64_t, uint32_t>; // For now, use 32-bit integers to count

template<typename SketchType> struct FinalSketch {using final_type = SketchType;};

#define FINAL_OVERLOAD(type) \
    template<> struct FinalSketch<type> {using final_type = typename type::final_type;}
FINAL_OVERLOAD(mh::CountingRangeMinHash<uint64_t>);
FINAL_OVERLOAD(mh::RangeMinHash<uint64_t>);
FINAL_OVERLOAD(mh::BBitMinHasher<uint64_t>);
FINAL_OVERLOAD(CBBMinHashType);

template<typename T>struct SketchFileSuffix {static constexpr const char *suffix = ".sketch";};
#define SSS(type, suf) template<> struct SketchFileSuffix<type> {static constexpr const char *suffix = suf;}
SSS(mh::CountingRangeMinHash<uint64_t>, ".crmh");
SSS(mh::RangeMinHash<uint64_t>, ".rmh");
SSS(mh::BBitMinHasher<uint64_t>, ".bmh");
SSS(CBBMinHashType, ".cbmh");
SSS(hll::hll_t, ".hll");

}
#endif /* SKETCH_HELPERS_H___ */
