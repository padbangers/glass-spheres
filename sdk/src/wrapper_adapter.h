// wrapper_adapter.h - run an engine written for the mpc-vst-plugins wrapper (engine.h: mpc_engine_t, string
// key/value params, int16 stereo in 128-frame blocks) as a plugin built with Akai's MPC Plugin SDK
// (framework/mpc_plugin.h).
//
// The SDK framework does the VST2 glue, the build (zig, arm32 + arm64), host_sim and the plugin folder. What this
// file adds is the old wrapper's behaviour, unchanged, so the existing skin (Plugin Skins/, bound to these exact
// parameter indices) and saved projects keep working:
//   * the parameter table is the generated params.h (tools/gen_vst.py of the pinned framework): same order, names,
//     units, options, popup flags and momentary params;
//   * the host talks to the engine as before: option Q-Link zones, unrounded shadows for integer params, popup
//     "open" flags (popup.h), step triggers, momentary holds, dynamic names and display texts, UpdateDisplay;
//   * the state chunk is the engine's own "state" text, as the wrapper saved it, so projects saved with the
//     wrapper builds load in this one and the other way round.
// The SDK's own setParameter/getParameter/dispatcher are replaced for those opcodes after mpc::detail::createEffect;
// everything else (process, MIDI in, transport, programs, I/O properties) goes through the SDK.
//
// Include after params.h. Behaviour follows wrapper/vst2_wrap.c at sd88me/mpc-vst-plugins 818f574 with the port's
// patches 0001-0003 (host transport, option Q-Link zones, one open popup). SPDX-License-Identifier: MIT
#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "mpc_plugin.h"

extern "C" {
#include "engine.h"
}
#include "popup.h"

#ifndef HAS_LFO_BPM
#define HAS_LFO_BPM 0
#endif
#ifndef OPTION_QLINK_ZONES
#define OPTION_QLINK_ZONES 0
#endif
#ifndef HAS_HOST_TRANSPORT
#define HAS_HOST_TRANSPORT 0
#endif
#ifndef MODULE_DIR
#define MODULE_DIR NULL
#endif
#ifdef PLUG_EFFECT
#define WRAP_IS_EFFECT 1
#else
#define WRAP_IS_EFFECT 0
#endif

// Optional read-only display parameters after the generated table (a port's views.h, included before this file):
// the engine answers get_param(key) with 0..1, the adapter pushes changes to the host about 30 times a second so
// the skin redraws them (MPC only redraws a value the plugin changes on its own when told), never sends anything
// to the engine for them, ignores the host's echo, and reports them as not automatable.
#ifndef WRAP_VIEWS
struct WrapView { const char* key; const char* name; };
static const WrapView VIEWS[1] = {{"", ""}};
#define NVIEWS 0
#endif

namespace wrap {

const int kDspBlock = 128;

inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

inline void copy_str(char* dst, const char* src, size_t max) {
    strncpy(dst, src, max - 1);
    dst[max - 1] = 0;
}

// normalized 0..1 -> engine value string
inline void norm_to_str(const param_t* p, float n, char* buf, int len) {
    if (p->nopts) snprintf(buf, len, "%d", (int)lroundf(clamp01(n) * (p->nopts - 1)));
    else if (p->int_display) snprintf(buf, len, "%ld", lroundf(p->min + (p->max - p->min) * clamp01(n)));
    else snprintf(buf, len, "%g", p->min + (p->max - p->min) * clamp01(n));
}

// engine value string (number or option label) -> normalized 0..1
inline float str_to_norm(const param_t* p, const char* s) {
    if (p->nopts) {
        int idx = -1;
        if (isdigit((unsigned char)s[0])) idx = atoi(s);
        else
            for (int i = 0; i < p->nopts; i++)
                if (!strcasecmp(s, p->opts[i])) idx = i;
        if (idx < 0) idx = 0;
        return p->nopts > 1 ? (float)idx / (p->nopts - 1) : 0;
    }
    return p->max > p->min ? clamp01((float)((atof(s) - p->min) / (p->max - p->min))) : 0;
}

#ifdef MODULE_SUBDIR
// The directory this library was loaded from (from /proc/self/maps; no dladdr, no function-local statics).
inline int plugin_dir(char* buf, size_t n) {
    buf[0] = 0;
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[1024];
    unsigned long me = (unsigned long)&plugin_dir;
    int ok = 0;
    while (!ok && fgets(line, sizeof line, f)) {
        unsigned long lo, hi;
        char* p = strchr(line, '/');
        if (sscanf(line, "%lx-%lx", &lo, &hi) == 2 && me >= lo && me < hi && p) {
            p[strcspn(p, "\n")] = 0;
            char* slash = strrchr(p, '/');
            if (slash && slash > p && (size_t)(slash - p) < n) { memcpy(buf, p, slash - p); buf[slash - p] = 0; ok = 1; }
        }
    }
    fclose(f);
    return ok;
}
#endif

class Adapter : public mpc::Plugin {
public:
    static mpc::PluginInfo makeInfo() {
        mpc::PluginInfo i;
        i.name = PLUG_NAME;
        i.vendor = PLUG_VENDOR;
        i.uniqueId = (uint32_t)PLUG_UID;
        i.version = PLUG_VERSION;
        i.isInstrument = !WRAP_IS_EFFECT;
        i.numInputs = WRAP_IS_EFFECT ? 2 : 0;
        i.numOutputs = 2;
        i.wantsMidi = true;   // the wrapper accepted host MIDI for effects too (MPC sends none to inserts)
        return i;
    }

    Adapter() : mpc::Plugin(makeInfo()) {
        // Every parameter is a plain 0..1 float to the SDK: the host value is the wrapper's normalized value, and the
        // wrapper logic below decides what reaches the engine. The SDK copy only feeds host_sim and the SDK dispatcher.
        for (int i = 0; i < NPARAMS; i++) {
            mpc::Param p = mpc::floatParam(PARAMS[i].key, PARAMS[i].name, 0.0f, 1.0f, PARAMS[i].def, PARAMS[i].unit, 3);
            p.automatable = true;
            addParam(p);
            shadow_[i] = -1;
        }
        for (int v = 0; v < NVIEWS; v++) {
            mpc::Param p = mpc::floatParam(VIEWS[v].key, VIEWS[v].name, 0.0f, 1.0f, 0.0f, "", 3);
            p.automatable = false;
            addParam(p);
            viewLast_[v] = -1.0f;
        }
        api_ = mpc_engine();
        if (!api_) return;
#if WRAP_IS_EFFECT
        if (!api_->process) { api_ = nullptr; return; }
#endif
#ifdef MODULE_SUBDIR
        char data_dir[600], here[512];
        const char* module_dir = MODULE_DIR;
        if (plugin_dir(here, sizeof here) && snprintf(data_dir, sizeof data_dir, "%s/%s", here, MODULE_SUBDIR) < (int)sizeof data_dir)
            module_dir = data_dir;
        dsp_ = api_->create(module_dir);
#else
        dsp_ = api_->create(MODULE_DIR);
#endif
    }

    ~Adapter() override {
        if (api_ && dsp_) api_->destroy(dsp_);
    }

    bool ok() const { return api_ != nullptr && dsp_ != nullptr; }

    // ---- parameters, as the wrapper did it ----------------------------------------------------------------
    float getNorm(int i) {
        char buf[64];
        if (i < 0 || i >= NPARAMS) return 0;
        if (popup_is(i)) return open_[i];
        if (PARAMS[i].string_display) {
            char k2[96];
            snprintf(k2, sizeof k2, "%s_on", PARAMS[i].key);
            if (api_->get_param(dsp_, k2, buf, sizeof buf) > 0) return atoi(buf) ? 1.0f : 0.0f;
        }
        if (api_->get_param(dsp_, PARAMS[i].key, buf, sizeof buf) <= 0) return PARAMS[i].def;
        float v = str_to_norm(&PARAMS[i], buf);
        if (PARAMS[i].int_display && !PARAMS[i].nopts && PARAMS[i].max > PARAMS[i].min && shadow_[i] >= 0) {
            float half = 0.5f / (PARAMS[i].max - PARAMS[i].min) + 1e-4f;
            if (fabsf(v - shadow_[i]) <= half) return shadow_[i];
            shadow_[i] = -1;
        }
        if (OPTION_QLINK_ZONES && PARAMS[i].nopts > 1 && shadow_[i] >= 0) {
            int n = PARAMS[i].nopts - 1;
            if (lroundf(shadow_[i] * n) == lroundf(v * n)) return shadow_[i];
            shadow_[i] = -1;
        }
        return v;
    }

    static bool isView(int i) { return i >= NPARAMS && i < NPARAMS + NVIEWS; }

    float viewValue(int v) {
        char buf[32];
        if (api_->get_param(dsp_, VIEWS[v].key, buf, sizeof buf) <= 0) return 0.0f;
        return clamp01((float)atof(buf));
    }

    float hostValue(int i) {
        if (isView(i)) return viewValue(i - NPARAMS);
        if (i < 0 || i >= NPARAMS) return 0;
        if (popup_is(i)) return popup_flag_value(open_, i);
        return getNorm(i);
    }

    void hostSet(int i, float n) {
        char buf[64];
        if (i < 0 || i >= NPARAMS) return;   // views are read-only: the host's echo of a push ends here
        const param_t* p = &PARAMS[i];
        int nudge = 0;
        if (popup_is(i)) {
            int dir = popup_set(open_, push_, i, n), t = p->popup_of;
            if (dir && PARAMS[t].nopts > 1) {   // the data wheel scrolls the list's option
                int idx = (int)lroundf(getNorm(t) * (PARAMS[t].nopts - 1)) + dir;
                if (idx < 0) idx = 0;
                if (idx > PARAMS[t].nopts - 1) idx = PARAMS[t].nopts - 1;
                norm_to_str(&PARAMS[t], (float)idx / (PARAMS[t].nopts - 1), buf, sizeof buf);
                api_->set_param(dsp_, PARAMS[t].key, buf);
                shadow_[t] = -1;
                needUpdateDisplay_ = 1;
            }
            return;
        }
        if (p->step_target >= 0) {   // a momentary nudge of another param; this key never reaches the engine
            if (n > 0.5f) {
                const param_t* tp = &PARAMS[p->step_target];
                if (api_->get_param(dsp_, tp->key, buf, sizeof buf) > 0) {
                    float cur = (float)atof(buf) + p->step_delta;
                    if (cur < tp->min) cur = tp->min;
                    if (cur > tp->max) cur = tp->max;
                    snprintf(buf, sizeof buf, "%g", cur);
                    api_->set_param(dsp_, tp->key, buf);
                }
                holdFrames_[i] = 1;
            }
            needUpdateDisplay_ = 1;
            return;
        }
        if (p->nopts > 1 && !OPTION_QLINK_ZONES) {   // a value between options is a nudge: step one option
            float pos = clamp01(n) * (p->nopts - 1);
            if (fabsf(pos - roundf(pos)) > 0.001f) {
                nudge = 1;
                float cur = getNorm(i) * (p->nopts - 1);
                int idx = (int)lroundf(cur) + (pos > cur ? 1 : -1);
                if (idx < 0) idx = 0;
                if (idx > p->nopts - 1) idx = p->nopts - 1;
                n = (float)idx / (p->nopts - 1);
            }
        }
        norm_to_str(p, n, buf, sizeof buf);
        api_->set_param(dsp_, p->key, buf);
        shadow_[i] = ((p->int_display && !p->nopts) || (OPTION_QLINK_ZONES && p->nopts > 1)) ? clamp01(n) : -1;
        if (p->momentary && n > 0.5f) holdFrames_[i] = p->hold_ms > 0 ? (int)(p->hold_ms * 44.1f) : 1;
        if (!nudge) popup_picked(open_, push_, i);
        needUpdateDisplay_ = 1;
    }

    void paramName(int idx, char* out) {
        if (isView(idx)) { copy_str(out, VIEWS[idx - NPARAMS].name, 32); return; }
        if (idx < 0 || idx >= NPARAMS) return;
        char buf[64], k2[96];
        snprintf(k2, sizeof k2, "%s_name", PARAMS[idx].key);
        if (PARAMS[idx].dynamic_name && api_->get_param(dsp_, k2, buf, sizeof buf) > 0) copy_str(out, buf, 32);
        else copy_str(out, PARAMS[idx].name, 32);
    }

    void paramDisplay(int idx, char* out) {
        char buf[64], k2[96];
        if (isView(idx)) { snprintf(out, 24, "%.2f", viewValue(idx - NPARAMS)); return; }
        const param_t* pp = &PARAMS[idx];
        snprintf(k2, sizeof k2, "%s_display", pp->key);
        if (pp->dynamic_display && api_->get_param(dsp_, k2, buf, sizeof buf) > 0) {
            copy_str(out, buf, 24);
        } else if (pp->nopts) {
            int k = (int)lroundf(getNorm(idx) * (pp->nopts - 1));
            copy_str(out, pp->opts[k], 24);
        } else if (api_->get_param(dsp_, pp->key, buf, sizeof buf) > 0) {
            if (pp->string_display) copy_str(out, buf, 24);
            else snprintf(out, 24, "%.*f", (pp->int_display || fabs(pp->max - pp->min) > 20) ? 0 : 1, atof(buf));
        }
    }

    intptr_t getChunk(void** out) {
        int len = api_->get_param(dsp_, "state", chunk_, sizeof chunk_);
        if (len <= 0) return 0;
        *out = chunk_;
        return (intptr_t)strlen(chunk_) + 1;
    }

    int setChunk(const void* data, intptr_t size) {
        if (size <= 0 || (size_t)size > sizeof chunk_) return 0;
        memcpy(chunk_, data, (size_t)size);
        chunk_[size - 1] = 0;
        api_->set_param(dsp_, "state", chunk_);
        // Momentary params are not part of the state: a loaded project starts with none of them pressed.
        for (int i = 0; i < NPARAMS; i++)
            if (PARAMS[i].momentary) { release(i); if (holdFrames_[i] > 0) { holdFrames_[i] = 1; } }
        return 1;
    }

    // ---- audio ----------------------------------------------------------------------------------------------
    void process(mpc::ProcessContext& ctx) override {
        const int n = ctx.numFrames;
        for (int k = 0; k < ctx.numMidiEvents; k++) {
            uint8_t m[3] = { ctx.midi[k].data[0], ctx.midi[k].data[1], ctx.midi[k].data[2] };
            api_->midi(dsp_, m, 3);
        }
        housekeeping(ctx, n);
        float* outL = ctx.outputs[0];
        float* outR = ctx.outputs[1];
#if WRAP_IS_EFFECT
        // In and out are usually the same buffers (in place): each frame's input is read before its output is written.
        const float* inL = ctx.inputs ? ctx.inputs[0] : nullptr;
        const float* inR = ctx.inputs ? ctx.inputs[1] : nullptr;
        int i = 0;
        while (i < n) {
            if (inpos_ == 0 && pos_ >= kDspBlock && n - i >= kDspBlock) {   // whole 128-frame block: no added latency
                for (int j = 0; j < kDspBlock; j++) {
                    inb_[2 * j] = f2s(inL ? inL[i + j] : 0.0f);
                    inb_[2 * j + 1] = f2s(inR ? inR[i + j] : 0.0f);
                }
                api_->process(dsp_, inb_, block_, kDspBlock);
                for (int j = 0; j < kDspBlock; j++) {
                    outL[i + j] = block_[2 * j] * (1.0f / 32768.0f);
                    outR[i + j] = block_[2 * j + 1] * (1.0f / 32768.0f);
                }
                i += kDspBlock;
                continue;
            }
            float xl = inL ? inL[i] : 0.0f, xr = inR ? inR[i] : 0.0f;   // read first: outL may alias inL
            float l = 0, r = 0;
            if (pos_ < kDspBlock) { l = block_[pos_ * 2] * (1.0f / 32768.0f); r = block_[pos_ * 2 + 1] * (1.0f / 32768.0f); pos_++; }
            outL[i] = l;
            outR[i] = r;
            inb_[2 * inpos_] = f2s(xl);
            inb_[2 * inpos_ + 1] = f2s(xr);
            if (++inpos_ == kDspBlock) { api_->process(dsp_, inb_, block_, kDspBlock); pos_ = 0; inpos_ = 0; }
            i++;
        }
#else
        for (int i = 0; i < n; i++) {
            if (pos_ >= kDspBlock) { api_->render(dsp_, block_, kDspBlock); pos_ = 0; }
            outL[i] = block_[pos_ * 2] * (1.0f / 32768.0f);
            outR[i] = block_[pos_ * 2 + 1] * (1.0f / 32768.0f);
            pos_++;
        }
#endif
    }

private:
    static int16_t f2s(float f) {
        f *= 32768.0f;
        return f >= 32767.0f ? 32767 : f <= -32768.0f ? -32768 : (int16_t)lrintf(f);
    }

    // A momentary param back to 0 in the engine too, as the host is told. (The wrapper only told the host, so an
    // engine that triggers on a 0 -> 1 edge never saw the next press unless the host sent the 0 itself.)
    // Step triggers never reach the engine.
    void release(int i) {
        if (PARAMS[i].momentary && PARAMS[i].step_target < 0) api_->set_param(dsp_, PARAMS[i].key, "0");
    }

    void automate(int i, float v) {
        if (host_ && effect_) host_(effect_, audioMasterAutomate, i, 0, nullptr, v);
    }

    // Host notifications are deferred to the audio callback, as the wrapper did: never from inside the host's own
    // setParameter call.
    void housekeeping(const mpc::ProcessContext& ctx, int n) {
        char buf[64];
        const mpc::Transport& t = ctx.transport;
        if (HAS_LFO_BPM && t.valid && t.tempoValid && t.bpm > 0 && fabs(t.bpm - bpm_) > 0.01) {
            bpm_ = t.bpm;
            snprintf(buf, sizeof buf, "%.2f", bpm_);
            api_->set_param(dsp_, "lfo_bpm", buf);
        }
        if (HAS_HOST_TRANSPORT && t.valid) {
            snprintf(buf, sizeof buf, "%d %.6f %.4f", t.playing ? 1 : 0, t.ppqValid ? t.ppqPos : -1.0,
                     t.tempoValid ? t.bpm : 0.0);
            api_->set_param(dsp_, "host_transport", buf);
        }
        for (int i = 0; i < NPARAMS; i++)
            if (holdFrames_[i] > 0 && (holdFrames_[i] -= n) <= 0) { holdFrames_[i] = 0; release(i); automate(i, 0.0f); }
        for (int i = 0; i < NPARAMS; i++)
            if (push_[i]) { push_[i] = 0; automate(i, hostValue(i)); }
        if (NVIEWS > 0 && (viewFrames_ += n) >= kViewFrames) {   // ~30 pushes a second, changed values only
            viewFrames_ = 0;
            for (int v = 0; v < NVIEWS; v++) {
                float val = viewValue(v);
                if (fabsf(val - viewLast_[v]) > 0.4f / 64.0f) { viewLast_[v] = val; automate(NPARAMS + v, val); }
            }
        }
        if (needUpdateDisplay_) {
            needUpdateDisplay_ = 0;
            requestDisplayUpdate();
            for (int i = 0; i < NPARAMS; i++) {   // list-tile selection: buttons are not re-read on UpdateDisplay
                if (!PARAMS[i].string_display) continue;
                char k2[96], b2[16];
                snprintf(k2, sizeof k2, "%s_on", PARAMS[i].key);
                if (api_->get_param(dsp_, k2, b2, sizeof b2) <= 0) continue;
                int on = atoi(b2) ? 1 : 0;
                if (lastOn_[i] != on + 1) { lastOn_[i] = (signed char)(on + 1); automate(i, (float)on); }
            }
        }
    }

    const mpc_engine_t* api_ = nullptr;
    void* dsp_ = nullptr;
    int16_t block_[kDspBlock * 2] = {};
    int pos_ = kDspBlock;
#if WRAP_IS_EFFECT
    int16_t inb_[kDspBlock * 2] = {};
    int inpos_ = 0;
#endif
    double bpm_ = 0;
    volatile int holdFrames_[NPARAMS] = {};
    float shadow_[NPARAMS];
    signed char lastOn_[NPARAMS] = {};
    volatile char needUpdateDisplay_ = 0;
    static const int kViewFrames = 1470;   // 1/30 s at 44.1 kHz
    int viewFrames_ = 0;
    float viewLast_[NVIEWS > 0 ? NVIEWS : 1];
    float open_[NPARAMS] = {};
    volatile char push_[NPARAMS] = {};
    char chunk_[8192] = {};
};

// ---- the VST2 entry points that differ from the SDK's ---------------------------------------------------------
inline Adapter* self(AEffect* e) { return static_cast<Adapter*>(static_cast<mpc::Plugin*>(e->object)); }

inline void setParameter(AEffect* e, VstInt32 i, float v) {
    Adapter* a = self(e);
    if (a) a->hostSet((int)i, v);
}

inline float getParameter(AEffect* e, VstInt32 i) {
    Adapter* a = self(e);
    return a ? a->hostValue((int)i) : 0.0f;
}

inline VstIntPtr dispatcher(AEffect* e, VstInt32 op, VstInt32 idx, VstIntPtr v, void* p, float o) {
    Adapter* a = self(e);
    if (a) {
        switch (op) {
        case effGetParamName:
            if (idx >= 0 && idx < NPARAMS + NVIEWS && p) a->paramName(idx, static_cast<char*>(p));
            return 1;
        case effGetParamLabel:
            if (idx >= 0 && idx < NPARAMS && p) copy_str(static_cast<char*>(p), PARAMS[idx].unit, 8);
            else if (Adapter::isView(idx) && p) static_cast<char*>(p)[0] = 0;
            return 1;
        case effGetParamDisplay:
            if (idx < 0 || idx >= NPARAMS + NVIEWS || !p) return 0;
            a->paramDisplay(idx, static_cast<char*>(p));
            return 1;
        case effCanBeAutomated: return idx >= 0 && idx < NPARAMS;   // views are not automatable
        case effString2Parameter: return 0;
        case effGetChunk: return p ? a->getChunk(static_cast<void**>(p)) : 0;
        case effSetChunk: return a->setChunk(p, v);
        default: break;
        }
    }
    return mpc::detail::dispatcher(e, op, idx, v, p, o);
}

}  // namespace wrap

#define MPC_WRAPPER_ENTRY()                                                                    \
    extern "C" MPC_EXPORT AEffect* VSTPluginMain(audioMasterCallback host) {                   \
        if (host != nullptr && host(nullptr, audioMasterVersion, 0, 0, nullptr, 0.0f) == 0)    \
            return nullptr;                                                                    \
        wrap::Adapter* a = new wrap::Adapter();                                                \
        if (!a->ok()) { delete a; return nullptr; }                                            \
        AEffect* e = mpc::detail::createEffect(a, host);                                       \
        if (e == nullptr) return nullptr;                                                      \
        e->setParameter = &wrap::setParameter;                                                 \
        e->getParameter = &wrap::getParameter;                                                 \
        e->dispatcher = &wrap::dispatcher;                                                     \
        return e;                                                                              \
    }
