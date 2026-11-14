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
// No ALSA on Windows: the native build only feeds the SDK host simulator, which has no MIDI port anyway.
#include "../../src/midi_out.h"
namespace midi_out {
Port* Open() { return nullptr; }
void Close(Port*) {}
void NoteOn(Port*, int, int, int) {}
void NoteOff(Port*, int, int) {}
void Control(Port*, int, int, int) {}
}  // namespace midi_out
#else
#include "../../src/midi_out.cc"
#endif
