// Build wrapper (the SDK compiles src/*.cpp only): see sdk/README.md.
#define _USE_MATH_DEFINES 1  // M_PI on Windows (native host_sim build)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#pragma GCC diagnostic ignored "-Wnarrowing"
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wc++11-narrowing"
#pragma clang diagnostic ignored "-Winfinite-recursion"
#endif
#ifndef TEST
#define TEST 1
#endif
#include "../../src/engine.cc"
