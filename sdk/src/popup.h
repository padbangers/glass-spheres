/* Popup "open" flags for any wrapper (wrapper/vst2_wrap.c, or a port's own hand-written one).
 * A layout `popup` widget needs a hidden "<key>__open" param (params.h: popup_of = the enum it opens).
 * The flag lives in the wrapper only: never sent to the engine, never saved in the chunk.
 * Include after params.h. Usage:
 *   setParameter:  if (popup_set(w->open, i, n)) return;          before anything else
 *                  ... set the value ...
 *                  if (!nudge) popup_picked(w->open, w->holdFrames, i);   exact option picked
 *   getParameter:  if (popup_is(i)) return w->open[i];
 *   chunk, engine polling: skip popup_is(i) params.
 *   processReplacing already reports holdFrames[] flags to the host with audioMasterAutomate(i, 0). */
#ifndef MPC_POPUP_H
#define MPC_POPUP_H

static inline int popup_is(int i) { return PARAMS[i].popup_of >= 0; }

/* What the host is told for a popup flag: 0.25 closed, 0.75 open (IndexedEnabling rounds them to 0 / 1). Off the
 * ends, so the data wheel (which keeps the tapped field's focus) can move it both ways; MPC clamps at 0 and 1. */
static inline float popup_flag_value(const float *open, int i) { return open[i] > 0.5f ? 0.75f : 0.25f; }

/* A value for popup flag i. A tap on the field sends 0 or 1 (toggling what the host was told): 1 opens the list and
 * closes any other, so only one is ever open, 0 closes it. Anything in between is the data wheel: it returns the
 * step (-1 / +1) for the caller to apply to the list's enum (PARAMS[i].popup_of), else 0. push[j]: tell the host
 * j's value again (from processReplacing; the host must not be re-entered from its own setParameter call). */
static inline int popup_set(float *open, volatile char *push, int i, float n) {
    push[i] = 1;   /* back to 0.25 / 0.75 after every event */
    push[PARAMS[i].popup_of] = 1;
    if (n > 0.001f && n < 0.999f) return n > popup_flag_value(open, i) ? 1 : -1;
    if (n >= 0.999f)
        for (int j = 0; j < NPARAMS; j++)
            if (j != i && popup_is(j) && open[j] > 0.5f) { open[j] = 0; push[j] = 1; push[PARAMS[j].popup_of] = 1; }
    open[i] = n >= 0.999f ? 1.0f : 0.0f;
    return 0;
}

/* A tap on the current entry is no change, so MPC sends nothing and the list stays open; the skin closes lists with
 * a tap outside them instead (Overcast/Glass Spheres skin/post_build.py: add_popup_catchers). Reporting the enum
 * off its option while open doesn't help: MPC rounds it back to that button, or (outside 0..1) shows no selection,
 * and the wheel then seems not to move the list. */

/* An option of param i picked exactly (a list button). A Q-Link nudge lands between options and
 * must not call this, so turning the knob leaves the list open. Closes i's open popup; the host
 * hears "open = 0" from processReplacing via holdFrames[], not from inside its own setParameter call. */
static inline void popup_picked(float *open, volatile char *push, int i) {
    /* Any option picked closes every open list, not only i's: a pick can switch a mode panel (when=), and a list
     * left open over the other panel stays on screen until something closes it. */
    if (PARAMS[i].nopts < 2) return;
    for (int j = 0; j < NPARAMS; j++)
        if (popup_is(j) && open[j] > 0.5f) { open[j] = 0; push[j] = 1; push[PARAMS[j].popup_of] = 1; }
}

#endif
