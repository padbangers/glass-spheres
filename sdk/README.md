# Glass Spheres, built with Akai's MPC Plugin SDK

This folder is an [MPC Plugin SDK](https://github.com/inmusic-software/mpc-plugin-builder-sdk) project for Glass
Spheres 1.1.0 and later. It needs MPC OS 3.10.1+ with **Preferences → Enhancements (Beta) → Enable Enhancements** and
**External Plugin Support** on. The engine is `../src` with `../third_party/eurorack` (the same code the Docker build
uses). CV out (`cv_out.cc`, the "MPC Private" port) and MIDI out (`midi_out.cc`, its own ALSA sequencer client) run
inside the MPC process as before: the SDK doesn't sandbox plugins, it only drops MIDI a plugin hands to the host.

## What is where

| Path | What |
|---|---|
| `plugin.json` | SDK manifest: name, vendor, uid `PbMb`, version, `file32`/`file64`, kind instrument (silent) |
| `src/GlassSpheres.cpp` | Entry point: the engine behind `wrapper_adapter.h` |
| `src/wrapper_adapter.h` | Runs an engine written for the mpc-vst-plugins wrapper (`engine.h`) inside the SDK's `mpc::Plugin`. Its parameter, popup, Q-Link zone and state behaviour matches the wrapper's, so the skin and saved projects keep working. Shared by all Padbangers plugins: change it in one repo, copy it to the others |
| `src/params.h` | Parameter table, **generated** (see below). The index order is the skin's binding: never reorder |
| `src/views.h` | Read-only display parameters after the table (index 150 on): where modulation has each knob of the main tab right now. The engine answers `view_<key>`; the adapter pushes changes about 30 times a second, ignores the host's echo and reports them as not automatable |
| `src/engine.h`, `src/popup.h` | Copied from sd88me/mpc-vst-plugins 818f574 (MIT) with this repo's patches applied |
| `src/x_*.cpp` | One-line wrappers that compile the engine sources (the SDK only compiles `src/*.cpp`). On Windows `x_engine_cv_out.cpp` and `x_engine_midi_out.cpp` are stubs: no jacks or ALSA there, and the native build only feeds the host simulator |
| `skins/` | The `Plugin Skins` of release 1.0.0, patched by the scripts below (originals in `tools/orig/`) |
| `skin.json` | What the skin scripts change: MANUAL tab and ink, main tab, knob components and knobs with a modulation view |
| `tools/manual_art.py` | Redraws the MANUAL tab from `skin/gen_layout.py` (`MANUAL`, `MANUAL_SIZE` = 15 px body text, `SUPPORT` = "Support the project: / Ko-Fi.com/padbangers" under the topic list). Needs Pillow and Titillium Web SemiBold/Bold (OFL) |
| `tools/mod_rings.py` | Modulation on the main tab: a dot around each modulated knob (65-frame strip under the knob, hidden while nothing modulates it). Shared with Overcast and Drips (toggle and locks are optional) |
| `tools/wide_tag.py` | Widens the Y / CLOCK tab's "Y" tag over the whole Y section (knobs and lists) in `sh_bg_1.png`; `skin/gen_layout.py` draws the same (`YC_Y_TAG`) |
| `build.py` | Runs the SDK's `tools/build.py` on this folder and adds the include path the engine needs (`CPLUS_INCLUDE_PATH`) |

## Build and test

```
python sdk/build.py                      # native + arm32 + arm64, Plugin Skins, validator
<sdk>/.sdk-tools/host_sim sdk/build/native/glass-spheres.<dll|dylib|so> test --wav sdk/build/test.wav
python <sdk>/tools/mpc_remote.py <ip> update sdk --drive <n> --yes    # then restart the MPC app
```

The SDK is found at `$MPC_SDK`, else `../mpc-plugin-builder-sdk` next to this repo. Output folder:
`sdk/build/Padbangers - VST - Glass Spheres/`. Skin scripts, in this order after copying a new rendered skin:
`manual_art.py <fonts>`, `mod_rings.py`, `wide_tag.py` (all can be run again).

**host_sim on the MPC itself** (the real target; standalone, the MPC app is not touched): build the simulator for ARM
with the SDK's zig and run it against the arm32 build:

```
zig c++ -target arm-linux-gnueabihf.2.31 -mcpu=cortex_a17 -std=c++17 -O2 -I<sdk>/framework \
    -o host_sim_arm <sdk>/tools/host-sim/host_sim.cpp -ldl -lpthread
scp host_sim_arm "sdk/build/Padbangers - VST - Glass Spheres/VST/glass-spheres-arm32.so" <mpc>:/tmp/
ssh <mpc> 'cd /tmp && ./host_sim_arm ./glass-spheres-arm32.so test --seed 1'
```

Expected results (2026-11-14, MPC X): no crash, 1.5 % CPU per block, all real parameters round-trip. Two reports
are not bugs:
- `instrument produced silence`: Glass Spheres renders silence by design; its outputs are the jacks or MIDI.
- `param ... "<X> Mod": saved ... but after effSetChunk ...`: the modulation views are live values, and host_sim
  renders two blocks after loading the state before it compares. They differ whenever its random sweep switched
  modulation on (depends on `--seed`).

## Changing things

- **Parameters:** edit `params.json` / `skin/gen_params.py` as before, then regenerate `src/params.h` with the pinned
  framework (patches applied): `python third_party/mpc-vst-plugins/tools/gen_vst.py vst.json --params-h` and copy
  `build/params.h` to `sdk/src/`. Only append. Bump `"version"` in `vst.json` (it becomes `PLUG_VERSION`) and in
  `plugin.json` together.
- **Skin:** the rendered skin still comes from the Docker pipeline (`skin/gen_layout.py` → `layout.conf` →
  `./build.sh`). Copy its `Plugin Skins` over `sdk/skins/`, delete `tools/orig/`, run the skin scripts.

## Compatibility with 1.0.0

- Same name, vendor, uid and parameter order. Projects saved with 1.0.x load in 1.1.0 and the other way round,
  because the state chunk is still the engine's own `key=value;` text.
- Folder layout is the SDK's (`VST/glass-spheres-arm32.so`) in `VST2 Plugins/`. Older MPC OS (3.9.x, `/sdcard/Synths`)
  needs the catalog release made from this build (`tools/sdk_release.py` in open-mpc/mpc-vst-plugins).
