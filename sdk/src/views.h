// Read-only display parameters after the generated table (wrapper_adapter.h): the modulation dots around the GLASS
// SPHERES tab's knobs. The engine answers get_param("view_<x>") (src/engine.cc); the skin binds them by index
// (NPARAMS + n, sdk/tools/mod_rings.py). Append only: the skin and the host know them by position.
#pragma once

#define WRAP_VIEWS 1

struct WrapView {
    const char* key;
    const char* name;
};

static const WrapView VIEWS[] = {
    {"view_t_rate", "T Rate Mod"},
    {"view_t_bias", "T Bias Mod"},
    {"view_t_jitter", "Jitter Mod"},
    {"view_deja_vu", "Deja Vu Mod"},
    {"view_x_steps", "Steps Mod"},
    {"view_x_spread", "Spread Mod"},
    {"view_x_bias", "X Bias Mod"},
};
#define NVIEWS ((int)(sizeof(VIEWS) / sizeof(VIEWS[0])))
