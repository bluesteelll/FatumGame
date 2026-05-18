// FlecsSaveRemap — definitions live in the header as `inline thread_local`
// (C++17 inline variables) to sidestep MSVC's C2492 prohibition on combining
// thread_local with dllexport on extern declarations. This translation unit
// exists only to anchor the module's object file presence; no symbol needed.

#include "FlecsSaveRemap.h"
