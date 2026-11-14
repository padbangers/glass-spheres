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
#if defined(_WIN32)
// No CV jacks on Windows: the native build only feeds the SDK host simulator.
#include "../../src/cv_out.h"
namespace cv_out {
void AddInstance() {}
void RemoveInstance() {}
void Claim(const void*, int) {}
void Release(const void*, int) {}
bool Owns(const void*, int) { return true; }
void Set(const void*, int, uint16_t) {}
void Flush() {}
}  // namespace cv_out
#else
#include "../../src/cv_out.cc"
#endif
